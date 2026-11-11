// Locate the packager independently of the working directory used by Finder.
#pragma once
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace aiverse::editor {
inline std::string findBuildTool(const std::string& dir, const std::string& engineDir) {
    if (const char* env = std::getenv("AIVERSE_BUILD_TOOL"); env && *env) return env;
#if defined(_WIN32)
    const char* name = "aiversebuild.exe";
#else
    const char* name = "aiversebuild";
#endif
    namespace fs = std::filesystem;
    // Prefer tools belonging to this editor (or explicit --tools-from). A source-tree
    // fallback also supports editor-only development builds in a separate build folder.
    std::vector<fs::path> roots = {dir, fs::path(dir) / ".." / ".." / ".."};
    if (!engineDir.empty()) {
        for (const char* sub : {"bin", "build/mac", "build/macos", "build/linux-release", "build/windows-msvc"})
            roots.push_back(fs::path(engineDir) / sub);
    }
    for (const auto& root : roots) {
        for (const char* config : {"", "Release", "Debug", "RelWithDebInfo", "MinSizeRel"}) {
            std::error_code ec;
            auto candidate = root / config / name;
            if (!fs::is_regular_file(candidate, ec)) continue;
            auto resolved = fs::canonical(candidate, ec);
            if (!ec) return resolved.string();
        }
    }
    return name; // Custom installations may supply the tool through PATH.
}
} // namespace aiverse::editor
