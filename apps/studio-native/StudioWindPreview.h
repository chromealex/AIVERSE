// HTML Studio «Ветер»: presentation-only preview wind and rig debug views for
// every model with hierarchical wind masks (<model>.wind.json). Session state,
// never saved into the map; UI-independent so it is testable headless.
#pragma once
#include "engine/data/Json.h"
#include "engine/render/WindRigModel.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace aiverse::studio {

struct StudioWindState {
    bool enabled = true;
    float speed = 6;            // m/s
    float directionDeg = 0;     // the wind blows TOWARDS this heading, 0 = +X, 90 = +Z
    float gust = 0.35f;         // 0..1
    float scale = 0.035f;       // metres of sway per m/s at mask 1, flex 1
    render::wind::WindDebugView debug = render::wind::WindDebugView::Off;
    uint64_t revision = 1;

    render::wind::SceneWind sceneWind(double seconds) const {
        render::wind::SceneWind w;
        if (!enabled) return w;
        const double a = double(directionDeg) * 3.14159265358979 / 180.0;
        w.x = float(std::cos(a) * speed);
        w.z = float(std::sin(a) * speed);
        w.seconds = float(std::fmod(seconds, 3600.0));
        w.gust = gust;
        w.metresPerMps = scale;
        return w;
    }
};

inline const char* windDebugName(render::wind::WindDebugView v) {
    switch (v) {
        case render::wind::WindDebugView::Parts: return "parts";
        case render::wind::WindDebugView::Depth: return "depth";
        case render::wind::WindDebugView::Weight: return "weight";
        case render::wind::WindDebugView::Skeleton: return "skeleton";
        default: return "off";
    }
}

inline Json studioWindStateJson(const StudioWindState& s) {
    Json j = Json::object();
    j.set("enabled", Json::fromBool(s.enabled));
    j.set("speed", Json::number(double(s.speed), 3));
    j.set("direction", Json::number(double(s.directionDeg), 2));
    j.set("gust", Json::number(double(s.gust), 3));
    j.set("scale", Json::number(double(s.scale), 4));
    j.set("debug", Json::string(windDebugName(s.debug)));
    return j;
}

// Atomic: validates every field before changing state.
inline void applyStudioWindPatch(StudioWindState& s, const Json& patch) {
    if (!patch.isObject()) throw std::invalid_argument("Wind patch must be an object");
    StudioWindState next = s;
    for (const auto& [key, value] : patch.members()) {
        const auto number = [&](float lo, float hi) {
            if (!value.isNumber()) throw std::invalid_argument("Wind '" + key + "' must be a number");
            const double v = value.toDouble();
            if (!std::isfinite(v) || v < lo || v > hi)
                throw std::invalid_argument("Wind '" + key + "' out of range");
            return float(v);
        };
        if (key == "enabled") {
            if (value.type() != Json::Type::Bool) throw std::invalid_argument("Wind 'enabled' must be boolean");
            next.enabled = value.boolean();
        } else if (key == "speed") next.speed = number(0, 60);
        else if (key == "direction") next.directionDeg = number(-360, 360);
        else if (key == "gust") next.gust = number(0, 1);
        else if (key == "scale") next.scale = number(0, 0.5f);
        else if (key == "debug") {
            const std::string v = value.str();
            using V = render::wind::WindDebugView;
            if (v == "off") next.debug = V::Off;
            else if (v == "parts") next.debug = V::Parts;
            else if (v == "depth") next.debug = V::Depth;
            else if (v == "weight") next.debug = V::Weight;
            else if (v == "skeleton") next.debug = V::Skeleton;
            else throw std::invalid_argument("Wind 'debug' must be off|parts|depth|weight|skeleton");
        } else throw std::invalid_argument("Unknown wind setting '" + key + "'");
    }
    next.revision = s.revision + 1;
    s = next;
}

// Mask library + profiles of a project, for the panel's curve list.
inline Json studioWindLibraryJson(const std::string& projectDir) {
    namespace fs = std::filesystem;
    const fs::path root(projectDir);
    const render::FileReader read = [root](const std::string& name, std::string& out) {
        if (name != "wind/masks.json" && name != "wind/profiles.json") return false;
        const fs::path file = root / name;
        std::error_code ec;
        if (fs::is_symlink(fs::symlink_status(file, ec)) || !fs::is_regular_file(file)) return false;
        if (fs::file_size(file, ec) > 1u << 20) return false;
        std::ifstream in(file, std::ios::binary);
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return bool(in) || in.eof();
    };
    std::vector<std::string> warnings;
    const auto data = render::wind::windProjectData(projectDir.empty() ? render::FileReader{} : read, &warnings);
    Json masks = Json::array();
    for (const auto& [name, index] : data->masks.namedMasks()) {
        Json m = Json::object();
        m.set("name", Json::string(name));
        m.set("index", Json::number(int64_t(index)));
        Json samples = Json::array();
        for (uint8_t v : data->masks.mask(index)) samples.push(Json::number(double(v) / 255.0, 3));
        m.set("samples", samples);
        masks.push(m);
    }
    Json profiles = Json::array();
    for (const auto& [name, profile] : data->profiles) {
        Json p = Json::object();
        p.set("name", Json::string(name));
        Json levels = Json::array();
        for (const auto& l : profile.levels) {
            Json lv = Json::object();
            lv.set("mask", Json::string(l.mask));
            lv.set("stiffness", Json::number(double(l.stiffness), 3));
            lv.set("flex", Json::number(double(l.flex), 3));
            levels.push(lv);
        }
        p.set("levels", levels);
        profiles.push(p);
    }
    Json w = Json::array();
    for (const auto& s : warnings) w.push(Json::string(s));
    Json out = Json::object();
    out.set("masks", masks);
    out.set("profiles", profiles);
    out.set("uniqueMasks", Json::number(int64_t(data->masks.size())));
    out.set("warnings", w);
    return out;
}

inline Json studioWindJson(const StudioWindState& s, const std::string& projectDir,
                           const Json& sceneStats = Json{}) {
    Json out = Json::object();
    out.set("settings", studioWindStateJson(s));
    out.set("library", studioWindLibraryJson(projectDir));
    out.set("scene", sceneStats.isObject() ? sceneStats : Json::object());
    return out;
}

} // namespace aiverse::studio
