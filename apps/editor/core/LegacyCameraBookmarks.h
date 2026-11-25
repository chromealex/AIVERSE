#pragma once
#include "engine/render/Math3D.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace aiverse::editor::legacy_camera {
struct SavedCamera {
    render::float3 target{0, 0, 0};
    float yaw = 0, pitch = 0.95f, distance = 30, fov = 0.75f;
    bool ortho = false, valid = false;
};

inline std::string projectKey(const std::string& path) {
    // Stable FNV-1a, never std::hash (which isn't stable across implementations).
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : path) { hash ^= c; hash *= 1099511628211ull; }
    constexpr char digits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[size_t(i)] = digits[hash & 15u]; hash >>= 4; }
    return out;
}

inline bool finite(const SavedCamera& c) {
    return std::isfinite(c.target.x) && std::isfinite(c.target.y) && std::isfinite(c.target.z) &&
           std::isfinite(c.yaw) && std::isfinite(c.pitch) && std::isfinite(c.distance) && std::isfinite(c.fov) &&
           c.pitch >= 0.01f && c.pitch <= 1.57f && c.distance > 0 && c.distance < 1000000 && c.fov > 0.01f && c.fov < 3.14f;
}

inline std::filesystem::path bookmarkFile(const std::filesystem::path& directory, const std::string& project) {
    return directory / ("studio_scene_cameras_" + projectKey(project) + ".txt");
}

inline std::array<SavedCamera, 6> read(const std::filesystem::path& file) {
    std::array<SavedCamera, 6> slots{};
    std::ifstream in(file);
    std::string version;
    if (!(in >> version) || version != "AIVERSE_CAMERA_BOOKMARKS_V1") return {};
    for (auto& s : slots) {
        int present = 0, ortho = 0;
        if (!(in >> present >> s.target.x >> s.target.y >> s.target.z >> s.yaw >> s.pitch >> s.distance >> s.fov >> ortho)) return {};
        s.ortho = ortho != 0;
        s.valid = present != 0 && finite(s);
    }
    return slots;
}
} // namespace aiverse::editor::legacy_camera
