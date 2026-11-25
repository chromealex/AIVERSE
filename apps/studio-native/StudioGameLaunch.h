// Native-only launch plan. Browser supplies an entry mode, never a path or command.
#pragma once
#include <array>
#include "apps/editor/core/Workspace.h"
#include "apps/editor/core/NativeBuild.h"
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace aiverse::studio {
struct StudioGameLaunch {
    std::string project; std::optional<size_t> map;
    // "Play from here": first match camera (target x, z; distance; pitch, yaw in degrees), Scene view's.
    std::optional<std::array<float,5>> camera;
};
inline StudioGameLaunch studioGameLaunch(const editor::Workspace& ws,bool currentMap) {
    if (ws.project.dir.empty()) throw std::runtime_error("Open a project before Play");
    if (ws.project.actorSession()) throw std::runtime_error("Play of actor-session projects needs the actor-session client; Studio host migration is pending (G1)");
    if (ws.map.dirty || ws.project.rulesDirty || ws.project.manifestDirty)
        throw std::runtime_error("Save changes before Play");
    const auto missing=editor::missingNativeModules(ws.project.rulesJson,editor::compiledNativeIds());
    if (!missing.empty()) throw std::runtime_error("This Studio build lacks project native module: "+missing.front());
    StudioGameLaunch plan{ws.project.dir, {}, std::nullopt};
    if (currentMap) {
        if (ws.map.path.empty()) throw std::runtime_error("Save the current map before Play");
        const auto path=std::filesystem::weakly_canonical(ws.map.path);
        const auto& maps=ws.project.manifest["maps"].items();
        for (size_t i=0;i<maps.size();++i) {
            const auto listed=std::filesystem::path(ws.project.dir)/maps[i].getStr("file");
            if (std::filesystem::weakly_canonical(listed)==path) { plan.map=i; break; }
        }
        if (!plan.map) throw std::runtime_error("Save the map into the project map list before Play");
    }
    return plan;
}
}
