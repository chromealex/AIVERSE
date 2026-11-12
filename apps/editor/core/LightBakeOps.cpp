#include "LightBakeOps.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace aiverse::editor {

namespace fs = std::filesystem;

std::string lightBakeMapRel(const Workspace& ws) {
    if (ws.map.path.empty()) return {};
    return "maps/" + fs::path(ws.map.path).filename().string();
}

bake::BakeSettings lightBakeSettings(const Workspace& ws) {
    bake::BakeSettings s;
    std::string err;
    if (const Json& b = ws.map.data.extra["bake"]; b.isObject()) bake::settingsFromJson(b.dump(-1), s, err);
    return s;
}

Json lightBakeInfo(const Workspace& ws) {
    Json r = Json::object();
    const std::string rel = lightBakeMapRel(ws);
    if (rel.empty() || ws.project.dir.empty()) { r.set("exists", Json::fromBool(false)); return r; }
    const std::string folder = bake::bakeFolderFor(rel);
    r.set("folder", Json::string(folder));
    const std::string dir = ws.project.dir;
    auto read = [&dir](const std::string& p, std::string& out) {
        std::ifstream f(dir + "/" + p, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), {});
        return true;
    };
    bake::LightmapData d;
    std::string err;
    if (!bake::readLightmapHeader(folder, read, d, err)) { r.set("exists", Json::fromBool(false)); return r; }
    r.set("exists", Json::fromBool(true));
    r.set("hash", Json::string(bake::hex64(d.hash)));
    r.set("mode", Json::string(bake::bakedModeName(d.mode)));
    try { r.set("settings", Json::parse(d.settingsJson)); r.set("stats", Json::parse(d.statsJson)); } catch (...) {}
    return r;
}

void registerLightBakeOps(Workspace& ws) {
    Workspace* w = &ws;
    ws.registerOp({"lightbake_get",
        "Baked lighting of the open map: its bake settings (the map's 'bake' block over the defaults) and what the bake folder "
        "maps/<map>.bake holds (hash, mode, stats: regions, pages, texels, probes). Modes: indirect (lightmap = indirect light, sun "
        "realtime), shadowmask (+ baked sun shadows for far / shadowless views, default), subtractive (sun baked too, cheapest on phones).",
        Json::parse("{}"), [w](const Json&) {
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("settings", Json::parse(bake::settingsToJson(lightBakeSettings(*w))));
            r.set("bake", lightBakeInfo(*w));
            return r;
        }});
    ws.registerOp({"lightbake_set",
        "Change the open map's bake settings (stored in the map's 'bake' block; save the map to keep them). Keys: quality "
        "(preview | medium | high preset), mode (indirect | shadowmask | subtractive), texelsPerUnit, pageSize, samples, bounces, "
        "aoDistance, aoStrength, denoise, indirectIntensity, maskSamples, probeSpacing, probeHeight, probeSamples, padding. "
        "Then lightbake_bake (needs the editor).",
        Json::parse(R"({"properties": {"quality": {"type": "string", "enum": ["preview", "medium", "high"]},
            "mode": {"type": "string", "enum": ["indirect", "shadowmask", "subtractive"]}, "texelsPerUnit": {"type": "number"},
            "pageSize": {"type": "integer"}, "samples": {"type": "integer"}, "bounces": {"type": "integer"}, "aoDistance": {"type": "number"},
            "aoStrength": {"type": "number"}, "denoise": {"type": "boolean"}, "indirectIntensity": {"type": "number"}, "maskSamples": {"type": "integer"},
            "probeSpacing": {"type": "number"}, "probeHeight": {"type": "number"}, "probeSamples": {"type": "integer"}, "padding": {"type": "integer"}}})"),
        [w](const Json& p) {
            bake::BakeSettings s = lightBakeSettings(*w);
            std::string err;
            if (!bake::settingsFromJson(p.dump(-1), s, err)) throw std::runtime_error(err);
            w->map.beginEdit();
            w->map.data.extra.set("bake", Json::parse(bake::settingsToJson(s)));
            w->map.touched();
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("settings", Json::parse(bake::settingsToJson(s)));
            return r;
        }});
    ws.registerOp({"lightbake_clear",
        "Delete the open map's baked lighting (the folder maps/<map>.bake): the game falls back to realtime lighting. "
        "Generated lightmap UVs next to models (*.lmuv) are kept.",
        Json::parse("{}"), [w](const Json&) {
            const std::string rel = lightBakeMapRel(*w);
            if (rel.empty() || w->project.dir.empty()) throw std::runtime_error("no saved map is open");
            const fs::path dir = fs::path(w->project.dir) / bake::bakeFolderFor(rel);
            std::error_code ec;
            const auto removed = fs::remove_all(dir, ec);
            if (ec) throw std::runtime_error(dir.string() + ": " + ec.message());
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("removedFiles", Json::number(int64_t(removed)));
            w->log("lightbake", "cleared " + bake::bakeFolderFor(rel));
            return r;
        }});
}

} // namespace aiverse::editor
