// Light baking (agent-lightbake): bake lightmaps + light probes of the open map with the engine's CPU
// path tracer (engine/bake), show progress, cancel, clear; keeps the renderer's baked lighting in sync
// with the map's bake folder (maps/<map>.bake). The static scene is what the Scene view draws with
// kDrawStatic (buildings, props: immobile types), captured for one frame; the terrain comes from the
// view's height function. MCP: lightbake_bake / lightbake_status here, lightbake_get / set / clear in
// core/LightBakeOps.cpp. docs/lightbake.md.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/LightBakeOps.h"
#include "engine/bake/SceneBuilder.h"
#include "engine/platform/Platform.h"
#include "engine/render/BakedLighting.h"
#include "modules/rts/view/RtsLights.h"

namespace aiverse::editor {

namespace fs = std::filesystem;
using namespace aiverse::render;

namespace {
float3 sq(float3 c) { return {c.x * c.x, c.y * c.y, c.z * c.z}; }

// Everything a bake needs, copied on the main thread (the job never touches the editor).
struct BakeInput {
    struct Draw { std::string path; std::vector<mat4> nodeWorld; std::vector<Material> materials; std::vector<uint8_t> still; };
    std::vector<Draw> draws, first;   // two Scene frames: nodes that moved between them (animation) are left out
    std::vector<float> heights;   // terrain samples, (nx x nz) at `step`
    int nx = 0, nz = 0;
    float step = 0.5f, ex = 0, ez = 0;
    bake::BakeScene::Sun sun;
    bake::BakeScene::Sky sky;
    std::vector<bake::BakeLight> lights;
    bake::BakeSettings settings;
    std::string projectDir, folder;   // folder relative to the project
    uint64_t oldHash = 0;
    bool force = false;
};
} // namespace

class LightBakeWindow : public EditorWindow {
public:
    const char* id() const override { return "lightbake"; }
    const char* title() const override { return "Запекание света"; }
    const char* group() const override { return "look"; }
    const char* dockArea() const override { return "right-bottom"; }
    int order() const override { return 8; }
    bool openByDefault() const override { return false; }

    ~LightBakeWindow() override {
        cancel_ = true;
        if (thread_.joinable()) thread_.join();
    }

    void update(Editor& ed, double) override {
        if (!registered_) { registerOps(ed); registered_ = true; }
        syncLoaded(ed);
        // capture: the callback was installed last frame and the Scene view has drawn since
        // two Scene frames between updates are kept (the first update drops what came before); a node is static when it
        // is drawn with the same matrix in both (windmill sails, idle animations are not)
        // (waits while the map's world preview is rebuilt after an edit: the Scene shows no objects then)
        if (state_ == kCapture) {
            if (ed.worldPending() && waitFrames_++ < 1200) {   // start over once the preview is current
                captureFrames_ = 0;
                input_.first.clear();
                return;
            }
            const int f = captureFrames_++;
            if (f == 0) input_.draws.clear();
            else if (input_.first.empty() && !input_.draws.empty() && f < 120) { input_.first.swap(input_.draws); input_.draws.clear(); }
            else if (!input_.draws.empty() || f >= 120) finishCapture(ed);   // >= 120: nothing static (or no Scene view): terrain only
        }
        if (state_ == kFinished) {
            thread_.join();
            state_ = kIdle;
            std::lock_guard<std::mutex> lk(m_);
            ed.ws().log("lightbake", message_, failed_);
            loadedStamp_ = {};   // reload the folder
            if (reloadModels_.exchange(false)) ed.reloadRules();   // new .lmuv sidecars: the view's models get their lightmap UVs
        }
    }

    void draw(Editor& ed) override {
        Workspace& ws = ed.ws();
        if (ws.project.dir.empty() || lightBakeMapRel(ws).empty()) { ui::callout(ui::Tone::Warn, "Сначала откройте и сохраните карту."); return; }
        bake::BakeSettings s = lightBakeSettings(ws);
        if (std::string e; !pending_.empty()) bake::settingsFromJson(pending_, s, e);   // still dragging
        bool changed = false;
        const char* modes[] = {"Только непрямой (солнце в реальном времени)", "Shadowmask (запечённые тени солнца)", "Subtractive (всё запечено, дешевле всего)"};
        int mode = int(s.mode);
        if (ImGui::Combo("Режим", &mode, modes, 3)) { s.mode = bake::BakedMode(mode); changed = true; }
        changed |= ImGui::SliderFloat("Текселей на метр", &s.texelsPerUnit, 0.5f, 32.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderInt("Сэмплов", &s.samples, 4, 1024, "%d", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderInt("Отскоков", &s.bounces, 0, 6);
        changed |= ImGui::SliderFloat("AO, дистанция", &s.aoDistance, 0, 5, "%.2f");
        changed |= ImGui::Checkbox("Шумоподавление", &s.denoise);
        changed |= ImGui::SliderFloat("Шаг световых проб", &s.probeSpacing, 0, 8, "%.1f");
        ui::help("Пробы освещают движущиеся объекты (юниты); 0 — без проб.");
        if (changed) pending_ = bake::settingsToJson(s);
        if (!pending_.empty() && !ImGui::IsAnyItemActive()) { ws.call("lightbake_set", Json::parse(pending_)); pending_.clear(); }
        ImGui::Separator();
        if (state_ == kIdle) {
            if (ImGui::Button("Запечь")) start(ed, false);
            ImGui::SameLine();
            if (ImGui::Button("Запечь заново")) start(ed, true);
            ui::help("«Запечь» пропускает работу, если сцена, свет и настройки не менялись (хеш содержимого).");
            ImGui::SameLine();
            if (ImGui::Button("Очистить")) { ws.call("lightbake_clear", Json::object()); loadedStamp_ = {}; }
        } else {
            std::string stage;
            { std::lock_guard<std::mutex> lk(m_); stage = stage_; }
            ImGui::ProgressBar(progress_.load(), ImVec2(-1, 0), stage.c_str());
            if (ImGui::Button("Отменить")) cancel_ = true;
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            if (!message_.empty()) ui::callout(failed_ ? ui::Tone::Error : ui::Tone::Ok, message_.c_str());
        }
        const Json info = lightBakeInfo(ws);
        if (info.getBool("exists")) {
            const Json& st = info["stats"];
            ui::toneText(ui::Tone::Dim, "Запечено: %s, %lld регионов, %lld стр., %lld текселей, %lld проб", info.getStr("mode").c_str(),
                         (long long)st.getInt("regions"), (long long)st.getInt("pages"), (long long)st.getInt("texels"), (long long)st.getInt("probes"));
        } else ui::toneText(ui::Tone::Dim, "Свет не запечён: только освещение в реальном времени.");
        bool on = ed.renderer().bakedQuality() != BakedQuality::Off;
        if (ImGui::Checkbox("Показывать запечённый свет", &on)) ed.renderer().setBakedQuality(on ? BakedQuality::High : BakedQuality::Off);
    }

private:
    enum State { kIdle, kCapture, kBaking, kFinished };

    void syncLoaded(Editor& ed) {
        Workspace& ws = ed.ws();
        const std::string rel = lightBakeMapRel(ws);
        const std::string folder = rel.empty() || ws.project.dir.empty() ? std::string() : ws.project.dir + "/" + bake::bakeFolderFor(rel);
        if (++checkTick_ % 30 != 0 && folder == loadedFolder_) return;
        std::error_code ec;
        const auto stamp = folder.empty() ? fs::file_time_type{} : fs::last_write_time(folder + "/lightmap.json", ec);
        if (folder == loadedFolder_ && stamp == loadedStamp_ && !ec == loadedOk_) return;
        loadedFolder_ = folder; loadedStamp_ = stamp; loadedOk_ = !ec;
        ed.renderer().setBakedLighting(nullptr);
        baked_.release();
        if (folder.empty() || ec) return;
        bake::LightmapData d;
        std::string err;
        auto read = [](const std::string& p, std::string& out) {
            std::ifstream f(p, std::ios::binary);
            if (!f) return false;
            out.assign(std::istreambuf_iterator<char>(f), {});
            return true;
        };
        if (!bake::loadLightmapData(folder, read, d, err)) { ws.log("lightbake", err, true); return; }
        if (baked_.upload(ed.device(), std::move(d))) ed.renderer().setBakedLighting(&baked_);
    }

    std::string start(Editor& ed, bool force) {
        Workspace& ws = ed.ws();
        if (state_ != kIdle) return "a bake is already running";
        if (ws.project.dir.empty() || lightBakeMapRel(ws).empty()) return "open and save a map first";
        if (ed.playing()) return "stop the play-test first (the bake captures the map as placed)";
        input_ = BakeInput{};
        input_.force = force;
        input_.settings = lightBakeSettings(ws);
        input_.projectDir = ws.project.dir;
        input_.folder = bake::bakeFolderFor(lightBakeMapRel(ws));
        const Json info = lightBakeInfo(ws);
        if (info.getBool("exists")) bake::parseHex64(info.getStr("hash"), input_.oldHash);
        ed.renderer().setModelCapture([this](const ModelDraw& d, const std::vector<mat4>& nodeWorld) {
            if (!d.model || d.model->path.empty() || (d.tint.w < 0.99f)) return;
            BakeInput::Draw dr;
            dr.path = d.model->path;
            dr.nodeWorld = nodeWorld;
            if (d.materials) dr.materials = *d.materials;
            input_.draws.push_back(std::move(dr));
        });
        captureFrames_ = 0; waitFrames_ = 0;
        ed.focusWindow("scene");   // the capture is what the Scene view draws: bring it to the front
        state_ = kCapture;
        { std::lock_guard<std::mutex> lk(m_); message_.clear(); failed_ = false; stage_ = "capture"; }
        progress_ = 0;
        cancel_ = false;
        return {};
    }

    void finishCapture(Editor& ed) {
        ed.renderer().setModelCapture({});
        // procedural shapes ("shape:...") have no file to load: they are not part of the bake
        std::erase_if(input_.draws, [](const BakeInput::Draw& d) { return d.path.rfind("shape:", 0) == 0; });
        std::erase_if(input_.first, [](const BakeInput::Draw& d) { return d.path.rfind("shape:", 0) == 0; });
        const bool compare = input_.first.size() == input_.draws.size();
        for (size_t i = 0; i < input_.draws.size(); ++i) {
            auto& d = input_.draws[i];
            d.still.assign(d.nodeWorld.size(), 1);
            if (!compare || input_.first[i].path != d.path || input_.first[i].nodeWorld.size() != d.nodeWorld.size()) continue;
            for (size_t n = 0; n < d.nodeWorld.size(); ++n)
                d.still[n] = std::memcmp(d.nodeWorld[n].m, input_.first[i].nodeWorld[n].m, sizeof d.nodeWorld[n].m) == 0;
        }
        input_.first.clear();
        // lighting as the Scene shows it now (time of day preview included)
        const Lighting& l = ed.renderer().lighting;
        input_.sun.dir = l.sunDir;
        input_.sun.color = sq(l.sunColor) * l.sunIntensity;
        input_.sky.sky = sq(l.skyColor);
        input_.sky.ground = sq(l.groundColor);
        const auto& grid = ed.editGrid();
        const MapDoc& doc = ed.doc();
        // map lights marked "bake": mixed / baked
        const Json mapJson = doc.toJson();
        auto lights = game::RtsLights::parseMapLights(mapJson["lights"]);
        for (const auto& ml : lights) {
            if (!ml.bake) continue;
            const PointLight p = game::RtsLights::glowLight(ml, {ml.x, ed.view().heightAt(grid, ml.x, ml.z), ml.z}, 0, 1, 0, 0);
            bake::BakeLight bl;
            bl.pos = p.pos; bl.color = p.color * p.intensity; bl.radius = p.radius;
            bl.dir = p.dir; bl.cosOuter = p.cosOuter; bl.cosInner = p.cosInner;
            bl.bakedOnly = ml.bake == 2;
            input_.lights.push_back(bl);
        }
        // terrain heights (the view's surface), half a cell apart
        input_.ex = float(doc.data.w); input_.ez = float(doc.data.h);
        input_.nx = int(std::ceil(input_.ex / input_.step)) + 1;
        input_.nz = int(std::ceil(input_.ez / input_.step)) + 1;
        input_.heights.resize(size_t(input_.nx) * size_t(input_.nz));
        for (int z = 0; z < input_.nz; ++z)
            for (int x = 0; x < input_.nx; ++x)
                input_.heights[size_t(z) * size_t(input_.nx) + size_t(x)] = ed.view().heightAt(grid, std::min(float(x) * input_.step, input_.ex), std::min(float(z) * input_.step, input_.ez));
        state_ = kBaking;
        thread_ = std::thread([this] { run(); });
    }

    void run() {
        const BakeInput& in = input_;
        auto read = [dir = in.projectDir](const std::string& p, std::string& out) {
            std::ifstream f(dir + "/" + p, std::ios::binary);
            if (f) { out.assign(std::istreambuf_iterator<char>(f), {}); return true; }
            return platform::readContentFile(p, out);
        };
        auto write = [dir = in.projectDir](const std::string& p, const std::string& bytes) {
            const fs::path file = fs::path(dir) / p;
            std::error_code ec;
            fs::create_directories(file.parent_path(), ec);
            std::ofstream f(file, std::ios::binary);
            f.write(bytes.data(), std::streamsize(bytes.size()));
            return bool(f);
        };
        auto finish = [&](const std::string& msg, bool failed) {
            { std::lock_guard<std::mutex> lk(m_); message_ = msg; failed_ = failed; }
            state_ = kFinished;
        };
        bake::BakeScene scene;
        bake::SceneBuilder sb(scene, read, write, in.settings.texelsPerUnit);
        std::string err, warnings;
        std::set<std::string> failed;
        for (const auto& d : in.draws)
            if (!failed.count(d.path) && !sb.addModel(d.path, d.nodeWorld, d.materials.empty() ? nullptr : &d.materials, true, 1, err, &d.still)) {
                warnings += err + "\n";   // once per model
                failed.insert(d.path);
            }
        const int nx = in.nx, nz = in.nz;
        auto height = [&in, nx, nz](float x, float z) {
            const float fx = std::clamp(x / in.step, 0.0f, float(nx - 1)), fz = std::clamp(z / in.step, 0.0f, float(nz - 1));
            const int x0 = std::min(int(fx), nx - 2), z0 = std::min(int(fz), nz - 2);
            const float tx = fx - float(x0), tz = fz - float(z0);
            auto h = [&](int a, int b) { return in.heights[size_t(b) * size_t(nx) + size_t(a)]; };
            return (h(x0, z0) * (1 - tx) + h(x0 + 1, z0) * tx) * (1 - tz) + (h(x0, z0 + 1) * (1 - tx) + h(x0 + 1, z0 + 1) * tx) * tz;
        };
        if (nx >= 2 && nz >= 2) sb.addTerrain(height, 0, 0, in.ex, in.ez, in.step, {0.30f, 0.28f, 0.24f});
        scene.sun = in.sun; scene.sky = in.sky; scene.lights = in.lights;
        if (sb.unwrapped) reloadModels_ = true;
        for (const auto& w : sb.warnings) warnings += w + "\n";
        const uint64_t hash = bake::hashBakeInput(scene, in.settings);
        if (!in.force && hash == in.oldHash) { finish("Без изменений: запечённый свет актуален (хеш " + bake::hex64(hash) + ").", false); return; }
        bake::LightmapData data;
        bake::BakeStats stats;
        const bool ok = bake::bakeLighting(scene, in.settings, data, err, [this](const std::string& stage, float f) {
            { std::lock_guard<std::mutex> lk(m_); stage_ = stage; }
            progress_ = f;
            return !cancel_.load();
        }, &stats);
        if (!ok) { finish("Ошибка запекания: " + err, true); return; }
        if (!bake::saveLightmapData(in.folder, data, write, err)) { finish("Ошибка записи: " + err, true); return; }
        char buf[256];
        std::snprintf(buf, sizeof buf, "Запечено за %.1f с: %d регионов, %d стр., %d текселей, %d проб, %d треугольников%s.", stats.seconds, stats.regions,
                      stats.pages, stats.texels, stats.probes, stats.triangles, sb.unwrapped ? ", созданы UV для лайтмапов" : "");
        finish(std::string(buf) + (warnings.empty() ? "" : "\n" + warnings), false);
    }

    void registerOps(Editor& ed) {
        Editor* e = &ed;
        ed.ws().registerOp({"lightbake_bake",
            "Bake the open map's lighting with the engine's path tracer: lightmaps for static objects (immobile types: buildings, props) "
            "and the terrain, light probes for moving ones, stored in maps/<map>.bake. Uses the map's bake settings (lightbake_set; "
            "parameters given here are saved there first) and the lighting the Scene shows now (sun, sky, map lights with \"bake\": "
            "\"mixed\" | \"baked\"). Runs in the background: poll lightbake_status. Skipped when nothing changed (content hash) unless force.",
            Json::parse(R"({"properties": {"force": {"type": "boolean"}, "quality": {"type": "string", "enum": ["preview", "medium", "high"]},
                "mode": {"type": "string", "enum": ["indirect", "shadowmask", "subtractive"]}, "texelsPerUnit": {"type": "number"},
                "samples": {"type": "integer"}, "bounces": {"type": "integer"}, "probeSpacing": {"type": "number"}}})"),
            [this, e](const Json& p) {
                Json settings = Json::object();
                for (const auto& [k, v] : p.members()) if (k != "force") settings.set(k, v);
                if (settings.size()) e->ws().call("lightbake_set", settings);
                const std::string err = start(*e, p.getBool("force"));
                if (!err.empty()) throw std::runtime_error(err);
                Json r = Json::object();
                r.set("ok", Json::fromBool(true));
                r.set("started", Json::fromBool(true));
                r.set("next", Json::string("lightbake_status until state is idle, then view_screenshot {\"window\": \"scene\"}"));
                return r;
            }, true});
        ed.ws().registerOp({"lightbake_status",
            "Progress of the running bake (state capture | baking | idle, stage, progress 0..1), the last result message and the bake folder info.",
            Json::parse("{}"), [this, e](const Json&) {
                Json r = Json::object();
                const int s = state_.load();
                r.set("state", Json::string(s == kIdle ? "idle" : s == kCapture ? "capture" : "baking"));
                std::lock_guard<std::mutex> lk(m_);
                r.set("stage", Json::string(stage_));
                r.set("progress", Json::number(double(progress_.load()), 3));
                r.set("message", Json::string(message_));
                r.set("failed", Json::fromBool(failed_));
                r.set("bake", lightBakeInfo(e->ws()));
                r.set("loaded", Json::fromBool(e->renderer().bakedLighting() != nullptr));
                r.set("lightmappedPrimitives", Json::number(int64_t(e->renderer().bakedLightmapped())));   // last drawn view
                r.set("probePrimitives", Json::number(int64_t(e->renderer().bakedProbed())));
                return r;
            }, true});
    }

    bool registered_ = false;
    std::string pending_;   // settings edited in the UI, written when the widget is released
    render::BakedLighting baked_;
    std::string loadedFolder_;
    fs::file_time_type loadedStamp_{};
    bool loadedOk_ = false;
    int checkTick_ = 0;
    std::atomic<int> state_{kIdle};
    std::atomic<float> progress_{0};
    std::atomic<bool> cancel_{false}, reloadModels_{false};
    std::mutex m_;
    std::string stage_, message_;
    bool failed_ = false;
    int captureFrames_ = 0, waitFrames_ = 0;
    BakeInput input_;
    std::thread thread_;
};

AIVERSE_EDITOR_WINDOW(LightBakeWindow);

} // namespace aiverse::editor
