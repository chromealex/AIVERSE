// game.json "window": how the game window opens on desktops (macOS, Windows, Linux).
//
//   "window": {
//     "mode": "windowed" | "fixed" | "maximized" | "fullscreen",   // fixed = windowed without resizing
//     "width": 1280, "height": 720,       // logical points of the window's content (windowed / fixed)
//     "resizable": true,                  // windowed only; "fixed" means false
//     "minWidth": 960, "minHeight": 540,  // smallest size when resizable (0 = no limit)
//     "keepAspect": false,                // resizing keeps width : height
//     "allowFullscreen": true,            // F11, Alt+Enter (Windows), the green button (macOS)
//     "macos": {...}, "windows": {...}, "linux": {...}   // per-platform overrides of the keys above
//   }
//
// Mobile apps are always fullscreen (orientation: game.json "orientation"); the web build ignores
// the block. Used by the game client (AppCallbacks::configure) and checked by the editor and aiversebuild.
#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "Platform.h"
#include "engine/data/Json.h"

namespace aiverse::platform {

inline const char* windowModeId(AppConfig::WindowMode m, bool resizable) {
    switch (m) {
        case AppConfig::WindowMode::Maximized: return "maximized";
        case AppConfig::WindowMode::Fullscreen: return "fullscreen";
        default: return resizable ? "windowed" : "fixed";
    }
}

// The block with the per-platform override merged in ("macos" / "windows" / "linux" keys).
inline Json windowBlockFor(const Json& window, const std::string& platform) {
    Json out = window.isObject() ? window : Json::object();
    if (window.isObject() && window[platform].isObject())
        for (auto& [k, v] : window[platform].members()) out.set(k, v);
    for (const char* p : {"macos", "windows", "linux"}) out.erase(p);
    return out;
}

// Problems of a "window" block (unknown mode, sizes out of range); empty = fine. Russian, for the editor.
inline std::vector<std::string> windowSettingsProblems(const Json& window) {
    std::vector<std::string> out;
    if (window.isNull()) return out;
    if (!window.isObject()) { out.push_back("\"window\" должен быть объектом"); return out; }
    auto check = [&](const Json& b, const std::string& where) {
        const std::string mode = b.getStr("mode", "windowed");
        if (mode != "windowed" && mode != "fixed" && mode != "maximized" && mode != "fullscreen")
            out.push_back(where + "mode: \"" + mode + "\" — нужно windowed, fixed, maximized или fullscreen");
        for (const char* k : {"width", "height", "minWidth", "minHeight"})
            if (b.has(k) && (!b[k].isNumber() || b[k].integer() < 0 || b[k].integer() > 16384)) out.push_back(where + k + ": число от 0 до 16384");
        if (b.has("width") && b.has("minWidth") && b["minWidth"].integer() > b["width"].integer()) out.push_back(where + "minWidth больше width");
        if (b.has("height") && b.has("minHeight") && b["minHeight"].integer() > b["height"].integer()) out.push_back(where + "minHeight больше height");
    };
    check(window, "");
    for (const char* p : {"macos", "windows", "linux"})
        if (window.has(p)) { if (window[p].isObject()) check(windowBlockFor(window, p), std::string(p) + ": "); else out.push_back(std::string(p) + ": должен быть объектом"); }
    return out;
}

// Applies the block to the config. keepSize: the size came from the command line (--size), leave it.
inline void applyWindowSettings(AppConfig& cfg, const Json& window, const std::string& platform, bool keepSize = false) {
    if (!window.isObject()) return;
    const Json b = windowBlockFor(window, platform);
    const std::string mode = b.getStr("mode", "windowed");
    cfg.windowMode = mode == "maximized" ? AppConfig::WindowMode::Maximized : mode == "fullscreen" ? AppConfig::WindowMode::Fullscreen
                                                                                                 : AppConfig::WindowMode::Windowed;
    cfg.resizable = mode == "fixed" ? false : b.getBool("resizable", true);
    auto dim = [&](const char* k, int def) { return b[k].isNumber() ? std::clamp(int(b[k].integer()), 0, 16384) : def; };
    if (!keepSize) {
        cfg.width = std::max(320, dim("width", cfg.width));
        cfg.height = std::max(240, dim("height", cfg.height));
    }
    cfg.minWidth = std::min(dim("minWidth", 0), cfg.width);
    cfg.minHeight = std::min(dim("minHeight", 0), cfg.height);
    cfg.keepAspect = b.getBool("keepAspect", false);
    cfg.allowFullscreen = b.getBool("allowFullscreen", true);
}

} // namespace aiverse::platform
