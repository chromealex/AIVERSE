#include "apps/studio-native/StudioHost.h"
#include "apps/studio-native/StudioHumanLookDocument.h"
#include "apps/studio-native/StudioGameInput.h"
#include "apps/studio-native/StudioTerrainGeometry.h"
#include "apps/editor/core/BrandingOps.h"
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

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "third_party/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "third_party/stb_image_write/stb_image_write.h"

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
        "studio.setProperty", "studio.save", "studio.undo", "studio.redo", "studio.nereidFlowSet",
        "studio.renderLightingSet", "studio.lightingSet", "studio.lightAdd", "studio.lightUpdate", "studio.lightDelete",
        "studio.materialPatch", "studio.materialCreate", "studio.setProperties", "studio.componentAdd",
        "studio.componentRemove", "studio.entityMove", "studio.entitiesDelete", "studio.entitiesDuplicate",
        "studio.assetPlace", "studio.entityFlags", "studio.rulesSet", "studio.entitiesAlign", "studio.historyJump"};
    return methods;
}
} // namespace

StudioHost::StudioHost(StudioHostPlatform& platform, const std::string& engineRoot, bool htmlComposite)
    : platform_(platform), engineRoot_(engineRoot), htmlComposite_(htmlComposite) {
    // The project opens asynchronously after the UI loads (splash and progress).
    session_ = std::make_unique<StudioSession>(engineRoot, std::string());
    session_->setMaterialSlotProvider([this](const std::string& asset) {
        return preview_ ? onScene([&] { return preview_->modelMaterialSlots(asset); }) : std::vector<std::string>{};
    });
}
StudioHost::~StudioHost() { shutdown(); }
void StudioHost::shutdown() {
    *alive_ = false;
    game_.reset(); // hosted client and its device retire before the native surface
    if (renderThread_) {
        // The Scene and its GPU resources are released on their owner thread, then the thread stops.
        try { renderThread_->invoke([&] { preview_.reset(); }); } catch (const std::exception&) {}
        renderThread_->stop();
        renderThread_.reset();
    }
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
    if (projectLoading_) return;
    projectLoading_ = true;
    if (game_) game_->stop();
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
Json StudioHost::gameState() {
    if (game_) return game_->state();
    auto out = Json::parse(R"({"paused":false,"canControl":false,"canStep":false,"active":false,"loading":false,"progress":0,"inMatch":false,"tick":0,"error":""})");
    out.set("device", StudioGameDevice{}.json());
    return out;
}
void StudioHost::gameEvent(const platform::Event& event) {
    if (game_ && platform_.surfaceDrawable(StudioSurface::Game)) game_->event(event);
}
void StudioHost::surfaceDetached(StudioSurface surface) {
    if (surface == StudioSurface::Scene) { placement_ = {}; readyEmitted_ = false; }
    else { gamePlacement_ = {}; if (game_) game_->suspend(); }
}
void StudioHost::tickGame() {
    const double now = platform_.now(), dt = gameFrameTime_ > 0 ? now - gameFrameTime_ : 0;
    gameFrameTime_ = now;
    if (!game_) return;
    if (!gamePlacement_.visible || !platform_.surfaceDrawable(StudioSurface::Game)) { game_->suspend(); return; }
    game_->frame(gamePlacement_.pixelsW, gamePlacement_.pixelsH, float(gamePlacement_.width), float(gamePlacement_.height),
                 dt, platform_.gameFocused());
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
        if (!preview_ || !session_->facialPreviewBinding().bound()) {
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
        onScene([&] { preview_->captureFacialThumbnail((out / (id + ".png")).string(), false); });
    } catch (const std::exception&) {}
}
void StudioHost::tick() {
    if (projectLoading_) return; // the background thread owns the session right now
    tickGame();
    // An empty Workspace owns an internal default MapDoc, NOT a loaded project.
    // Never render that provisional map as if it belonged to the user.
    if (session_->workspace().project.dir.empty()) return;
    // Agent panel: tool calls from aiversemcp run here, on the main thread, between frames.
    if (session_->pollAgentLink()) {
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
    if (!placement_.visible || !platform_.surfaceDrawable(StudioSurface::Scene)) {
        session_->setFacialPreviewGpuStatus(false, "Native Scene viewport hidden or not drawable");
        return;
    }
    try {
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
                created->setOrientationGizmo(!composite);
                // Interactive viewport: redraw on change only, debounce lighting rebuilds.
                // AIVERSE_STUDIO_CONTINUOUS_REDRAW=1 restores drawing every tick.
                created->setContinuousRedraw(always);
                preview_ = std::move(created);
            });
        }
        // Character and material previews read live session state (facial binding, material drafts):
        // the UI thread waits for those frames. Everything else renders asynchronously.
        const bool facialBound = session_->facialPreviewBinding().bound();
        const bool materialPreview = !session_->previewMaterialName().empty() && session_->previewMaterialFactors().isObject();
        if (!facialBound && !materialPreview) { tickScene(); return; }
        FrameOutputs out;
        const bool wantClips = (lightingStatusTicks_ % 30) == 0;
        std::vector<std::string> clips;
        float baseHeight = 0;
        renderThread_->flush(); // the last asynchronous map frame finished
        renderThread_->invoke([&] {
            platform_.sceneThreadScope([&] {
                const auto ids = session_->selectedObjectIds();
                preview_->setEditorState(std::set<std::string>(ids.begin(), ids.end()), session_->hiddenIds(), session_->lockedIds(), hoverId_);
                preview_->setWindPreview(session_->windPreview());
                const auto& ws = session_->workspace();
                preview_->frame(placement_.pixelsW, placement_.pixelsH, ws.map, ws.map.data.worldSpace, session_->selectedObjectId(),
                    session_->previewMaterialName(), session_->previewMaterialFactors(), session_->previewMaterialRevision(),
                    float(ws.project.rules.levelHeight.toDouble()), ws.project.dir, ws.project.rulesJson, &session_->facialPreviewBinding());
                out.facialReady = preview_->facialReady();
                out.facialError = preview_->facialError();
                if (wantClips) { clips = preview_->facialClips(); baseHeight = preview_->facialBaseHeight(); }
                out.lightingStatus = preview_->lightingStatus();
                out.load = preview_->modelLoadProgress();
                out.camera = preview_->cameraState();
                out.thumbnails = preview_->takeFinishedThumbnails();
                out.drawCalls = preview_->drawCalls();
            });
        });
        {
            // A map frame that finished before this one is superseded.
            std::lock_guard<std::mutex> io(renderIoMutex_);
            renderOutputsFresh_ = false;
        }
        if (wantClips) session_->setFacialPreviewClips(clips, baseHeight);
        publishSceneFrame(out);
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
        if (!rulesSnapshot_ || rulesSnapshotProject_ != ws.project.dir || hash != rulesSnapshotHash_) {
            rulesSnapshot_ = std::make_shared<const Json>(ws.project.rulesJson);
            rulesSnapshotHash_ = hash; rulesSnapshotProject_ = ws.project.dir;
        }
        nextRulesCheck_ = now + 1.0;
    }
    FrameInputs in;
    in.width = placement_.pixelsW; in.height = placement_.pixelsH;
    in.map = mapSnapshot_; in.rules = rulesSnapshot_;
    in.fps = ws.map.data.worldSpace;
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
            preview_->setWindPreview(in.wind);
            preview_->frame(in.width, in.height, *in.map, in.fps, in.selected, "", Json::object(), 0,
                            in.levelHeight, in.project, *in.rules, nullptr);
            out.facialReady = preview_->facialReady();
            out.facialError = preview_->facialError();
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
    session_->setFacialPreviewGpuStatus(out.facialReady, out.facialError);
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
    {
        ViewportRect hidden;
        NativeViewportPlacement none;
        (void)platform_.placeSurface(StudioSurface::Scene, hidden, none);
    }
    {
        std::lock_guard<std::mutex> io(renderIoMutex_);
        renderOutputsFresh_ = false;
    }
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
    if (projectLoading_) return false;
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
    std::map<std::string, double> current;
    for (const auto& component : inspected["result"]["components"].items()) {
        if (component.getStr("id") != "Transform") continue;
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
        set.set("componentId", Json::string("Transform"));
        set.set("values", values);
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
bool StudioHost::updateBrushCursor(double x, double y, double w, double h) {
    if (!preview_) return false;
    const auto& b = session_->sceneBrush();
    render::float3 at{};
    if (!b.armed || x < 0 || y < 0 || !onScene([&] { return preview_->groundPoint(float(x), float(y), float(w), float(h), at); })) {
        onScene([&] { preview_->setBrushCursor(false, {}, 0); });
        return b.armed;
    }
    const float r = b.kind == "road" ? 0.6f : b.kind == "river" ? float(b.width * 0.5) : float(b.radius);
    onScene([&] { preview_->setBrushCursor(true, at, r); });
    return true;
}
void StudioHost::brushAdjust(double factor, bool disarm) {
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
void StudioHost::rebuildOverlay() {
    if (!preview_) return;
    auto shapes = extShapes_;
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
    onScene([&] { preview_->setOverlayShapes(std::move(shapes)); });
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
bool StudioHost::terrainStroke(int phase, double x, double y, double w, double h) {
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
    if (!onScene([&] { return preview_->groundPoint(float(x), float(y), float(w), float(h), at); })) return phase != 0;
    updateBrushCursor(x, y, w, h);
    const bool ok = phase == 0 ? session_->strokeBegin(at.x, at.z, &error) : session_->strokeDab(at.x, at.z, &error);
    if (!ok && !error.empty()) report();
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
void StudioHost::select(double x, double y, double w, double h, const std::string& mode) {
    if (projectLoading_ || !preview_) return;
    if (session_->sculptBrush().regionPick || session_->sculptBrush().armed) {
        // Human Creator: a face blend region choice, or a sculpt brush stroke on the character.
        const bool region = session_->sculptBrush().regionPick;
        const auto& face = session_->facialPreviewBinding().face();
        vivid::HumanFaceHit hit;
        Json payload = Json::object();
        std::string pickError;
        if (face && onScene([&] { return preview_->sculptPick(float(x), float(y), float(w), float(h), *face, hit, &pickError); })) {
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
        return;
    }
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
    if (files.empty() || files.size() > 64 || session_->workspace().project.dir.empty()) return;
    try {
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
    auto& m = mouse_;
    if (button == 1) { // right: look, Alt zooms
        if (phase == 1) { if (mods.alt) zoom((dx + dy) * 0.6); else look(dx, dy); }
        return;
    }
    if (button == 2) { if (phase == 1) pan(dx, dy); return; }
    if (phase == 0) {
        m = {};
        m.active = true; m.pressX = x; m.pressY = y;
        // Spline handles (Roads / Water "Edit nodes"): LMB on a handle drags it.
        if (!mods.alt && splineDrag(0, x, y, w, h)) { m.spline = true; return; }
        // Scene brush armed: plain LMB paints; Alt still orbits.
        if (!mods.alt && brushArmed()) { m.stroke = terrainStroke(0, x, y, w, h); if (m.stroke) return; }
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
        if (m.spline) { splineDrag(1, x, y, w, h); return; }
        if (m.stroke) { terrainStroke(1, x, y, w, h); return; }
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
    if (st.spline) { splineDrag(2, 0, 0, w, h); return; }
    if (st.stroke) { terrainStroke(2, 0, 0, w, h); return; }
    if (st.move) { moveEnd(); return; }
    // Shift adds to the selection, Command toggles.
    const std::string mode = mods.command ? "toggle" : mods.shift ? "add" : "replace";
    if (st.marquee) { marqueeEnd(st.pressX, st.pressY, x, y, w, h, mode); return; }
    if (st.dragged || std::hypot(x - st.pressX, y - st.pressY) >= 5.0) return;
    if (gizmoAt(x, y, w, h)) return;
    select(x, y, w, h, mode);
}
void StudioHost::sceneWheel(double amount) { zoom(amount); }
bool StudioHost::sceneKey(StudioSceneKey key, bool down, StudioSceneMods mods, bool rightHeld) {
    using K = StudioSceneKey;
    if (!down) {
        if (key == K::V) { setPivotSnap(false); return true; }
        return false;
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
    if (id != "scene") {
        // Another panel's rect: the Scene view is not there.
        ViewportRect hidden;
        NativeViewportPlacement none;
        if (platform_.placeSurface(StudioSurface::Scene, hidden, none)) placement_ = {};
        return;
    }
    NativeViewportPlacement placed;
    if (!platform_.placeSurface(StudioSurface::Scene, rect, placed)) return;
    placement_ = placed;
    if (!placement_.visible) readyEmitted_ = false;
}
Json StudioHost::viewportInput(const Json& input) {
    const std::string kind = input.getStr("kind");
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
    if (kind == "handle") {
        const double x = input["x"].toDouble(), y = input["y"].toDouble(), w = input["width"].toDouble(), h = input["height"].toDouble();
        const std::string phase = input.getStr("phase");
        if (!input["x"].isNumber() || !input["y"].isNumber() || !input["width"].isNumber() || !input["height"].isNumber() ||
            !finite({x, y, w, h}) || w < 2 || h < 2 || w > 20000 || h > 20000) throw std::runtime_error("Invalid move coordinates");
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
            onScene([&] {
                preview_->setOrthographic(set.getBool("ortho"));
                preview_->tweenCamera({float(x), float(y), float(z)}, float(std::clamp(dist, 0.5, 100000.0)), float(yaw), float(pitch));
            });
        }
        const auto cam = onScene([&] { return preview_->cameraState(); });
        result.set("x", Json::number(double(cam.target.x), 3)); result.set("y", Json::number(double(cam.target.y), 3));
        result.set("z", Json::number(double(cam.target.z), 3)); result.set("distance", Json::number(double(cam.distance), 3));
        result.set("yaw", Json::number(double(cam.yaw), 5)); result.set("pitch", Json::number(double(cam.pitch), 5));
        result.set("ortho", Json::fromBool(cam.ortho));
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
        select(x, y, w, h, "replace");
        return ok();
    }
    throw std::runtime_error("Unsupported viewport input");
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
    if (!hasId) return false;
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
        std::error_code ec;
        if (path.empty() || !fs::exists(path, ec)) throw std::runtime_error("Nothing built there yet");
        platform_.revealInFileManager(path);
        Json r = Json::object(); r.set("ok", Json::fromBool(true)); return r;
    });
    if (method == "studio.humanMakeupAlign") return answer([&] {
        // Makeup layers placed from the shown face (needs the GPU preview's CPU copy).
        if (!preview_) throw std::runtime_error("Покажите персонажа во вьюпорте");
        Json r = Json::object(); r.set("values", onScene([&] { return preview_->alignMakeup(session_->facialPreviewBinding()); })); return r;
    });
    if (method == "studio.humanLookCapture") return answer([&] {
        // Preset card picture: the next character frame is cropped around the head and written beside the look.
        if (!preview_ || !session_->facialPreviewBinding().bound())
            throw std::runtime_error("Покажите персонажа во вьюпорте, чтобы снять превью");
        const auto path = studioHumanLookThumbnailFile(session_->workspace().project.dir, params.getStr("name"), true);
        onScene([&] { preview_->captureFacialThumbnail(path.string()); });
        Json r = Json::object(); r.set("ok", Json::fromBool(true)); return r;
    });
    if (method == "studio.viewportInput") return answer([&] { return viewportInput(params); });
    if (method == "studio.gameRegions") return answer([&] {
        const auto& project=session_->workspace().project.dir;
        if(project.empty()||params.getStr("expectedProject")!=project)
            throw std::runtime_error("World Explorer project changed; refresh the panel");
        if(game_)return game_->regions();
        Json out=Json::object();
        out.set("active",Json::fromBool(false));out.set("available",Json::fromBool(false));
        out.set("epoch",Json::string("0"));out.set("tick",Json::number(int64_t(0)));
        out.set("reason",Json::string("No local match is running"));return out;
    });
    if (method == "studio.gameEntities" || method == "studio.gameInspect") return answer([&] {
        if (method == "studio.gameEntities") {
            if (!params["offset"].isNumber() || !params["limit"].isNumber() || !params["query"].isString() ||
                params["offset"].toDouble() < 0 || params["offset"].toDouble() > 10000000 ||
                params["limit"].toDouble() < 1 || params["limit"].toDouble() > 100) throw std::runtime_error("Invalid runtime page");
            const auto offset = int(params["offset"].integer()), limit = int(params["limit"].integer());
            if (game_) return game_->entities(offset, limit, params.getStr("query"));
            StudioRuntimeInspect empty;
            return empty.entities(nullptr, "0", offset, limit, params.getStr("query"));
        }
        if (!game_) throw std::runtime_error("No game is running");
        return game_->inspect(params.getStr("epoch"), params.getStr("ref"));
    });
    if (method == "studio.keepPlayChanges") {
        // The running game's map entities (positions, amounts, removed) go to the map as one History step.
        Json reply = Json::object();
        reply.set("id", req["id"]);
        try {
            if (!game_) throw std::runtime_error("No game is running");
            const auto states = game_->mapEntityStates();
            Json r = Json::object(), p = Json::object();
            p.set("entities", states["entities"]);
            p.set("removeDead", Json::fromBool(params.getBool("removeDead")));
            r.set("id", req["id"]); r.set("method", Json::string("studio.keepPlayChanges")); r.set("params", p);
            reply = session_->dispatch(r);
            if (!reply.has("error")) sendEvent("document.changed", "");
        } catch (const std::exception& e) { reply.set("error", Json::string(e.what())); }
        sendReply(reply);
        return true;
    }
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
            auto plan = studioGameLaunch(session_->workspace(), entry == "map");
            if (entry == "map" && params.getBool("fromScene") && preview_) {
                const auto cam = onScene([&] { return preview_->cameraState(); });
                plan.camera = std::array<float, 5>{cam.target.x, cam.target.z, cam.distance, float(cam.pitch * 57.29578), float(cam.yaw * 57.29578)};
            }
            ensureGame();
            game_->start(plan, engineRoot_ + "/engine/assets");
            gameFrameTime_ = 0;
        } else if (method == "studio.gameStop" && game_) game_->stop();
        else if (method == "studio.gameControl") {
            if (!game_) throw std::runtime_error("No game is running");
            game_->control(params.getStr("action"));
        }
        return gameState();
    });
    if (method == "studio.hubTemplates") {
        // The catalog query runs aiverse_pkg.py over the network: keep the UI thread free.
        const int64_t requestId = req["id"].integer();
        auto result = std::make_shared<Json>();
        auto error = std::make_shared<std::string>();
        auto alive = alive_;
        platform_.runInBackground(
            [engineDir = session_->workspace().engineDir, params, result, error] {
                try { *result = hubTemplatesJson(engineDir, params); }
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
    if (method == "studio.select" && platform_.hasFloatingWindows()) sendEvent("selection.changed", ""); // floats follow it
    if (preview_ && (method == "studio.openMap" || method == "studio.newMap")) {
        onScene([&] { preview_->resetCamera(); });
        mapSnapshot_.reset(); rulesSnapshot_.reset();
    }
    if (method == "studio.materialPreview") sendEvent("material.preview", req["params"].getStr("name"));
    if (documentChangingMethods().count(method) || (method == "studio.op" && reply["result"].getBool("changed")))
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
        if (game_ && (method == "studio.openMap" || method == "studio.newMap")) game_->stop();
        if (projectLoading_ && method != "studio.viewportRect") { sendError(req["id"], "Project is loading"); return; }
        Json reply;
        if (method == "studio.assetThumbnail" && req["id"].isNumber()) {
            reply = Json::object();
            reply.set("id", req["id"]);
            try { reply.set("result", assetThumbnail(req["params"].getStr("path"))); }
            catch (const std::exception& e) { reply.set("error", Json::string(e.what())); }
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
            result.set("history", history);
            result.set("frames", Json::number(int64_t(sceneFrameCount_)));
            if (preview_) {
                result.set("drawCalls", Json::number(int64_t(onScene([&] { return preview_->drawCalls(); }))));
                if (req["params"].getBool("textures")) result.set("gpu", onScene([&] { return preview_->textureResources(); }));
            } else result.set("error", Json::string("The Scene panel is not visible"));
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
