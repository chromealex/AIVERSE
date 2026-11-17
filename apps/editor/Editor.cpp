// Editor: lifecycle, docking layout, menus, documents, play-test and viewport rendering.
// Window contents live in apps/editor/windows/*.cpp; the agent link in EditorAgent.cpp.
#include "Editor.h"
#include "windows/StudioContextMenus.h"
#include "core/SimTools.h"
#include "core/RouteOps.h"
#include "engine/fx/FxModelMesh.h"
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
#include "modules/rts/RtsTiles.h"
#include "modules/rts/view/RtsCamera.h"
#include "modules/rts/view/RtsOverlayData.h"
#include <sstream>

namespace fs = std::filesystem;

namespace aiverse::editor {

using namespace render;

namespace {
// Models / textures of the open game (view blocks reference them relative to the game folder).
FileReader projectReader(const std::string& dir) {
    return [dir](const std::string& p, std::string& out) {
        std::ifstream f(dir + "/" + p, std::ios::binary);
        if (!f) return platform::readContentFile(p, out);
        std::stringstream ss; ss << f.rdbuf(); out = ss.str();
        return true;
    };
}
} // namespace

// ============================ viewport ============================

void Viewport::ensure(gfx::Device& dev, int pw, int ph) {
    pw = std::max(pw, 8); ph = std::max(ph, 8);
    if (pw == w && ph == h && color.valid()) return;
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
}

void Viewport::release(gfx::Device& dev) {
    if (color.valid()) dev.destroyTexture(color);
    if (depth.valid()) dev.destroyTexture(depth);
    color = depth = {};
    w = h = 0;
}

void Viewport::image(gfx::Device& dev, float wPts, float hPts) {
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
    dev_ = &dev;
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
    r3d_.init(dev);
    postFx_.init(dev);
    postFx_.setQuality(render::PostFx::defaultQualityFor(dev, false));
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
    editorTypeLastUsed_.assign(rules().types.size(), -1.0e30);
    view_.setDeferredLoading(lazyEditorAssets_);
    view_.setDeferAppearanceVariants(false); // editing always exposes every variant
    view_.setContentBase(ws_.project.dir.empty() ? "" : ws_.project.dir + "/");
    view_.init(dev, rules(), projectReader(ws_.project.dir));
    for (auto& w : view_.warnings()) ws_.log("view", w, true);
    fxTextures_.init(dev);
    gameFx_.setQuality(fx::defaultQualityFor(dev, false));
    gameFx_.setFileReader([this](const std::string& path, std::string& out) { return projectReader(ws_.project.dir)(path, out); });
    gameFx_.setTextureLoader([this](const std::string& name, fx::FxTexture& out) {
        render::TextureRef r = fxTextures_.resolve(name, false, ws_.project.dir.empty() ? "" : ws_.project.dir + "/");
        if (!r.valid()) return false;
        out = {r.tex, r.u0, r.v0, r.u1, r.v1};
        return true;
    });
    gameFx_.setGroundHeight([this](float x, float z) { return sim_ ? view_.heightAt(simModule()->state(*sim_).grid, x, z) : view_.heightAt(grid_, x, z); });
    gameFx_.setMeshLoader([this](const std::string& name, fx::FxMesh& out) { return fx::meshFromModelFile(name, projectReader(ws_.project.dir), out); });
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

void Editor::shutdown() {
    if (!dev_) return;
    clientHost_.stop();
    gameFxBridge_.detach();
    sim_.reset();
    gameFx_.shutdown();
    fxTextures_.shutdown();
    windows_.clear(); // windows release their viewports in their destructors via update()? keep simple: textures die with the device
    if (studioLogo_.valid()) { dev_->destroyTexture(studioLogo_); studioLogo_ = {}; }
    view_.shutdown(*dev_);
    postFx_.shutdown();
    if(fpsPreviewMesh_.vb.valid())dev_->destroyBuffer(fpsPreviewMesh_.vb);
    if(fpsPreviewMesh_.ib.valid())dev_->destroyBuffer(fpsPreviewMesh_.ib);
    r3d_.shutdown();
    ui_.shutdown();
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
    reloadRules();
    selection = {};
    showPicker_ = false;
    fitScene();
    status = "Игра: " + ws_.project.manifest.getStr("name", idOrDir);
    checkNativeModules(false);
    if(ws_.project.isFps()) {
        focusWindow("fps");
        const auto& spawn=doc().data.fps["spawns"][0];
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
    view_.shutdown(*dev_);
    editorTypeLastUsed_.assign(rules().types.size(), -1.0e30);
    lastEditorTrim_ = time_;
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

void Editor::retainEditorType(uint16_t type) {
    if (type >= rules().types.size()) return;
    if (editorTypeLastUsed_.size() != rules().types.size())
        editorTypeLastUsed_.assign(rules().types.size(), -1.0e30);
    editorTypeLastUsed_[type] = time_;
    // The Model/Materials windows and model_info must work when the type has
    // no instances on the map. The RtsView loader caches this call per type.
    if (lazyEditorAssets_) view_.loadType(type);
}

void Editor::reclaimEditorAssets(bool force) {
    if (!lazyEditorAssets_ || !dev_ || playing() || clientPlaying() ||
        ws_.project.isFps()) return;
    constexpr double kSweepInterval = 5.0;
    constexpr double kUnusedGrace = 15.0;
    if (!force && time_ - lastEditorTrim_ < kSweepInterval) return;
    lastEditorTrim_ = time_;
    const size_t typeCount = rules().types.size();
    if (editorTypeLastUsed_.size() != typeCount) return;
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
        if (time_ - editorTypeLastUsed_[i] < kUnusedGrace) keep[i] = true;
    if (selection.kind == Selection::Type)
        for (size_t i = 0; i < typeCount; ++i)
            if (rules().types[i].id == selection.key) keep[i] = true;
    if (tool == Tool::Place && placeType >= 0 && size_t(placeType) < typeCount)
        keep[size_t(placeType)] = true;
    if (overlayPreviewType >= 0 && size_t(overlayPreviewType) < typeCount)
        keep[size_t(overlayPreviewType)] = true;
    // This only releases models no longer referenced by any kept TypeLook.
    // GPU work from their last use is older than the grace interval.
    view_.trimTypes(keep);
}

void Editor::newMap(int w, int h) {
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
    game::CameraSetup cs = game::cameraSetupFrom(ws_.project.manifest);
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

bool Editor::groundAt(const Viewport& vp, const RTSCamera& cam, float sx, float sy, float& x, float& z) const {
    float3 g;
    if (view_.planetMode()) { // cells on the face under the cursor (planet tools use planetPosAt)
        const Json at = planetPosAt(vp, sx, sy);
        if (!at.isObject()) return false;
        x = float(at["x"].toDouble()); z = float(at["y"].toDouble());
        return true;
    }
    // the drawn surface under the cursor (high ground, detail relief): march the view ray against
    // RtsView::heightAt, then bisect; the y = 0 plane only when the ray never meets it
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

int Editor::pickEntity(const Viewport& vp, const RTSCamera& cam, float sx, float sy) const {
    if (view_.planetMode()) { // nearest entity on the picked body, 3D distance
        int body; float3 local;
        if (!planetAt(vp, sx, sy, body, local)) return -1;
        int best = -1;
        float bestD = 1e9f;
        const auto& d = ws_.map.data;
        for (size_t i = 0; i < d.entities.size(); ++i) {
            int eb; float3 el;
            if (!planetLocal(d.entities[i].at, eb, el) || eb != body) continue;
            if (objectHidden(d.entities[i].owner, d.entities[i].type) || objectLocked(d.entities[i].owner, d.entities[i].type)) continue;
            const int t = rules().findType(d.entities[i].type);
            const float r = t >= 0 ? const_cast<game::RtsView&>(view_).selectionRadius(uint16_t(t)) + 0.2f : 0.6f;
            const float dd = length(el - local);
            if (dd < r && dd < bestD) { best = int(i); bestD = dd; }
        }
        return best;
    }
    // Nearest 3D bounds along the view ray: roof in front wins over a ground hit.
    float3 rayOrigin{}, rayDirection{};
    if (cam.screenRay(sx - vp.originX, sy - vp.originY, vp.pointsW, vp.pointsH, rayOrigin, rayDirection)) {
        const auto& candidates = ws_.map.data.entities;
        float closest = 1.0e30f;
        int picked = -1;
        auto slab = [](float origin, float direction, float lo, float hi, float& nearT, float& farT) {
            if (std::fabs(direction) < 0.000001f) return origin >= lo && origin <= hi;
            const float a = (lo - origin) / direction, b = (hi - origin) / direction;
            nearT = std::max(nearT, std::min(a, b));
            farT = std::min(farT, std::max(a, b));
            return nearT <= farT;
        };
        for (size_t i = 0; i < candidates.size(); ++i) {
            const auto& entity = candidates[i];
            if (objectHidden(entity.owner, entity.type) || objectLocked(entity.owner, entity.type)) continue;
            const int ti = rules().findType(entity.type);
            const auto& allTypes = rules().types;
            const bool structure = ti >= 0 && allTypes[size_t(ti)].structure;
            const float radius = ti >= 0 ? const_cast<game::RtsView&>(view_).selectionRadius(uint16_t(ti)) : .6f;
            const float halfX = structure ? std::max(.5f, float(allTypes[size_t(ti)].footprintW) * .5f) : std::max(.35f, radius);
            const float halfZ = structure ? std::max(.5f, float(allTypes[size_t(ti)].footprintH) * .5f) : std::max(.35f, radius);
            const float baseY = const_cast<game::RtsView&>(view_).heightAt(editGrid(), entity.x, entity.z);
            const float topY = baseY + (structure ? std::max(2.f, radius * 2.f) : std::max(.9f, radius * 2.f));
            float nearT = 0.f, farT = closest;
            if (!slab(rayOrigin.x, rayDirection.x, entity.x - halfX, entity.x + halfX, nearT, farT) ||
                !slab(rayOrigin.y, rayDirection.y, baseY, topY, nearT, farT) ||
                !slab(rayOrigin.z, rayDirection.z, entity.z - halfZ, entity.z + halfZ, nearT, farT)) continue;
            if (nearT < closest) { closest = nearT; picked = int(i); }
        }
        if (picked >= 0) return picked;
    }
    // Use visual, screen-space picking before the legacy ground hit-test. A ground
    // ray can land behind a roof on angled cameras and select a neighbouring unit.
    // Pixel-sized hit areas remain usable when zoomed out, while footprints scale
    // with the viewport. Only real map entities participate here.
    const auto& d = ws_.map.data;
    const float px = sx - vp.originX, py = sy - vp.originY;
    int nearestScreen = -1;
    float bestScore = 1e9f;
    for (size_t i = 0; i < d.entities.size(); ++i) {
        const auto& e = d.entities[i];
        if (objectHidden(e.owner, e.type) || objectLocked(e.owner, e.type)) continue;
        const int t = rules().findType(e.type);
        const float radius = t >= 0 ? const_cast<game::RtsView&>(view_).selectionRadius(uint16_t(t)) + 0.2f : 0.6f;
        const float groundY = const_cast<game::RtsView&>(view_).heightAt(editGrid(), e.x, e.z);
        float2 at, edge, top;
        if (!cam.worldToScreen({e.x, groundY, e.z}, vp.pointsW, vp.pointsH, at)) continue;
        if (!cam.worldToScreen({e.x + radius, groundY, e.z}, vp.pointsW, vp.pointsH, edge)) continue;
        const float height = (t >= 0 && rules().types[size_t(t)].structure) ? 2.0f : 0.8f;
        const bool hasTop = cam.worldToScreen({e.x, groundY + height, e.z}, vp.pointsW, vp.pointsH, top);
        const float hitRadius = std::clamp(std::hypot(edge.x - at.x, edge.y - at.y), 9.0f, 110.0f);
        // Test from base to approximate roof/upper silhouette, not just ground position.
        const float dx = px - at.x, dy = py - at.y;
        const float tx = hasTop ? top.x - at.x : 0.0f;
        const float ty = hasTop ? top.y - at.y : 0.0f;
        const float len2 = tx * tx + ty * ty;
        const float along = len2 > 0.01f ? std::clamp((dx * tx + dy * ty) / len2, 0.0f, 1.0f) : 0.0f;
        const float dist = std::hypot(dx - along * tx, dy - along * ty);
        const float score = dist / hitRadius;
        if (score <= 1.0f && score < bestScore) { bestScore = score; nearestScreen = int(i); }
    }
    if (nearestScreen >= 0) return nearestScreen;
    // Fallback for low-angle/special rendering modes where projection can fail.
    float x, z;
    if (!groundAt(vp, cam, sx, sy, x, z)) return -1;
    int best = -1;
    float bestD = 1e9f;
    for (size_t i = 0; i < d.entities.size(); ++i) {
        const auto& e = d.entities[i];
        if (objectHidden(e.owner, e.type) || objectLocked(e.owner, e.type)) continue;
        const int t = rules().findType(e.type);
        const float r = t >= 0 ? const_cast<game::RtsView&>(view_).selectionRadius(uint16_t(t)) + 0.2f : 0.6f;
        const float dd = std::hypot(x - e.x, z - e.z);
        if (dd < r && dd < bestD) { best = int(i); bestD = dd; }
    }
    return best;
}

int Editor::pickRoad(const Viewport& vp, const RTSCamera& cam, float sx, float sy) const {
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
            int x0, y0; game::RtsModule::footprintAt(t, {Fixed::fromRatio(int64_t(std::lround(e.x * 1000)), 1000), Fixed{}, Fixed::fromRatio(int64_t(std::lround(e.z * 1000)), 1000)}, x0, y0);   // editor only: float -> fixed
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

void Editor::buildEditScene(game::RtsScene& out) const {
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
        if (e.extra.has("facing")) {   // degrees clockwise from north (-z), as RtsModule spawns it: 0 north, 90 east, 180 south
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
        float4 c = game::RtsView::playerColor(int(p + 1));
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

void Editor::renderScene(Viewport& vp, const RTSCamera& cam, bool editOverlays) {
    if(ws_.project.isFps()){if(vp.color.valid())renderFpsScene(vp,cam);return;}
    if (!vp.color.valid()) return;
    if (playing()) { renderGame(vp, cam); return; }
    const bool traceSceneStages = std::getenv("AIVERSE_FRAME_TRACE") != nullptr;
    const auto stageStart = std::chrono::steady_clock::now();
    buildEditScene(scene_);
    if (lazyEditorAssets_) {
        // This includes generated world-preview objects and scripted spawns
        // that are not in MapDoc::entities. Don't repeatedly retain each
        // instance of the same type within a frame.
        for (const auto& o : scene_.objects)
            if (!o.isProjectile && o.type < editorTypeLastUsed_.size() &&
                editorTypeLastUsed_[o.type] != time_) retainEditorType(o.type);
    }
    const auto afterBuild = std::chrono::steady_clock::now();
    fieldPreview_.fill(ws_.project.rules, ws_.map, scene_);   // grid fields: the map's initial values
    const auto afterFields = std::chrono::steady_clock::now();
    const bool planets = view_.planetMode();
    // planet maps: the orbit camera of the view (Scene window input drives it)
    const RTSCamera& useCam = planets ? view_.updatePlanetCamera(0, float(ImGui::GetIO().DeltaTime), vp.h) : cam;
    r3d_.beginScene(useCam, vp.w, vp.h);
    setOverlayContext(vp, useCam, false);
    view_.draw(r3d_, scene_, time_, nullptr, grid_);
    const auto afterView = std::chrono::steady_clock::now();
    if (const auto& oc = view_.overlays().context(); overlayPreviewType >= 0 && oc.placeType >= 0)   // overlay preview ghost
        view_.drawGhost(r3d_, uint16_t(oc.placeType), oc.placeAt, oc.placeOk, grid_, time_);
    if (editOverlays && !planets) drawEditOverlays3D();
    if (!planets) tickFx(cam, vp.w, vp.h, nullptr);   // agent previews (fx_preview) in the Scene view
    gfx::PassDesc pass;
    pass.colorTarget = vp.color;
    pass.depthTarget = vp.depth;
    pass.clearColor[0] = 0.13f; pass.clearColor[1] = 0.15f; pass.clearColor[2] = 0.17f;
    if (planets) pass.clearColor[0] = pass.clearColor[1] = pass.clearColor[2] = 0.0f;
    // Accurate diagnostic colours must not pass through post-processing.
    const auto beforeRender = std::chrono::steady_clock::now();
    r3d_.render(pass, r3d_.studioDebugMode() ? nullptr : (postFxInScene ? &postFx_ : nullptr));
    const auto afterRender = std::chrono::steady_clock::now();
    if (!r3d_.studioDebugMode()) gameFx_.render({r3d_.lighting.fogColor, r3d_.lighting.fogDensity});
    r3d_.resolvePostFx();
    dev_->endPass();
    view_.drawOverlays(useCam, scene_, vp.pointsW, vp.pointsH, vp.originX, vp.originY, ImGui::GetWindowDrawList());
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

void Editor::renderGame(Viewport& vp, const RTSCamera& cam) {
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
    game::RtsScene sc;
    game::sceneFromSim(*sim_, sc, float(sim_->alpha()), uint8_t(human_));
    sc.paintLayers = paintLayers;
    sc.entityFx = entityFx;
    const bool sceneView = &vp == sceneViewport;   // the Scene shows the editor's selection / hover of the world tree
    for (auto& o : sc.objects) {
        o.selected = std::find(gameSelection.begin(), gameSelection.end(), o.entity) != gameSelection.end();
        if (sceneView && o.entity.valid()) {
            Selection me{Selection::Live, -1, {}};
            me.ref = worldRef(o.entity);
            o.selected = std::find(selectionSet.begin(), selectionSet.end(), me) != selectionSet.end();
            o.hovered = hover == me;
        }
    }
    r3d_.beginScene(cam, vp.w, vp.h);
    setOverlayContext(vp, cam, true);
    view_.draw(r3d_, sc, time_, human_ ? &gs.vision[size_t(human_)] : nullptr, gs.grid);
    if (const auto& oc = view_.overlays().context(); overlayPreviewType >= 0 && oc.placeType >= 0)
        view_.drawGhost(r3d_, uint16_t(oc.placeType), oc.placeAt, oc.placeOk, gs.grid, time_);
    tickFx(cam, vp.w, vp.h, &sc.objects);
    gfx::PassDesc pass;
    pass.colorTarget = vp.color;
    pass.depthTarget = vp.depth;
    pass.clearColor[0] = 0.55f; pass.clearColor[1] = 0.66f; pass.clearColor[2] = 0.78f;
    r3d_.render(pass, &postFx_);
    gameFx_.render({r3d_.lighting.fogColor, r3d_.lighting.fogDensity});
    r3d_.resolvePostFx();
    dev_->endPass();
    view_.drawOverlays(cam, sc, vp.pointsW, vp.pointsH, vp.originX, vp.originY, ImGui::GetWindowDrawList());
}

// Overlays get what the player does in this view: cursor, held keys, the structure being placed
// (Scene: the Place tool; both: overlay_preview), pause / match over.
void Editor::setOverlayContext(const Viewport& vp, const RTSCamera& cam, bool play) {
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
    if(ws_.project.isFps()){startFpsPlay();return;}
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
        sim_ = std::make_unique<Simulation>(cfg, std::make_unique<game::RtsModule>());
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
    human_ = humanSlot;
    gameFx_.clear();
    fxPreview_ = {};
    fxFrozen_ = false;
    gameFxBridge_.setHeightFn([this](float x, float z) { return sim_ ? view_.heightAt(simModule()->state(*sim_).grid, x, z) : 0.0f; });
    gameFxBridge_.setPlayerColorFn([](int p) { return game::RtsView::playerColor(p); });
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
    game::applyCameraSetup(game::cameraSetupFrom(ws_.project.manifest), gameCam, 720.0f);
    focusWindow("game");
    status = human_ ? "Тест: вы играете за игрока " + std::to_string(human_) : "Тест: боты играют на карте";
}

void Editor::stopPlay() {
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
    clientHost_.event(e);   // the hosted game client: pointer inside its screen, keys while the Game window has focus
    if (e.type == platform::EventType::QuitRequested) quit_ = true;
    if (e.type == platform::EventType::OpenFile) requestOpenProject(e.path);   // double-clicked .aiverse (macOS)
}

void Editor::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    const bool mod = io.KeyCtrl || io.KeySuper;
    if (showPicker_ || io.WantTextInput) return;
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
    auto t0 = std::chrono::steady_clock::now();
    const bool traceEditor = std::getenv("AIVERSE_FRAME_TRACE") != nullptr;
    time_ += dt;
    pollAgentLink();
    applyChanges();
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
    const auto afterWindows = std::chrono::steady_clock::now();
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
