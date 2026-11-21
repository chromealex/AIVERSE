// AIVERSE LENS: reusable, genre-independent startup loader for cameras.json.
// The application owns World and SceneHierarchy and explicitly calls this at
// a safe initialization point. No implicit mutation of gameplay camera state.
#pragma once
#include "CameraSceneAsset.h"
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace aiverse {
inline bool loadProjectCameras(World& world,SceneHierarchy& hierarchy,
                               const std::filesystem::path& projectDirectory,
                               std::vector<Entity>* created=nullptr,
                               std::string* error=nullptr) {
    const auto path=projectDirectory/"cameras.json";
    try {
        if(!std::filesystem::is_regular_file(path)) {
            if(error)*error="Camera asset not found: "+path.string();
            return false;
        }
        const Json asset=Json::parseFile(path.string());
        return loadCameraScene(world,hierarchy,asset,created,error);
    } catch(const std::exception& ex) {
        if(error)*error=ex.what();
        return false;
    }
}
} // namespace aiverse
