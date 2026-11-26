// Where the engine's files are (engine/assets, tools, sources for aiversebuild) for host tools: the editor,
// aiversebuild, assetpipe. A developer build uses the source tree it was compiled from; a packaged engine
// (e.g. the Windows build: <root>/bin/*.exe next to <root>/engine) is found next to the executable,
// so the folder can be moved or unpacked anywhere. AIVERSE_ENGINE_DIR overrides both.
#pragma once
#include <cstdlib>
#include <filesystem>
#include <string>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace aiverse::platform {

inline std::filesystem::path hostExecutablePath(const char* argv0) {
    namespace fs = std::filesystem;
    std::error_code ec;
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (size && _NSGetExecutablePath(path.data(), &size) == 0) {
        auto actual = fs::canonical(path.c_str(), ec);
        if (!ec) return actual;
    }
#elif defined(_WIN32)
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (size && size < path.size()) return fs::path(path.substr(0, size));
#elif defined(__linux__)
    auto actual = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return actual;
#endif
    return argv0 && *argv0 ? fs::absolute(argv0, ec) : fs::path();
}

inline bool looksLikeEngineDir(const std::filesystem::path& d) {
    std::error_code ec;
    // tools/pkg: the package tool (sample games are catalog packages now); templates/: older engine folders
    return std::filesystem::is_directory(d / "engine" / "assets", ec) &&
           (std::filesystem::is_directory(d / "tools" / "pkg", ec) || std::filesystem::is_directory(d / "templates", ec));
}

inline std::string resolveEngineDir(const std::string& compiled, const char* argv0) {
    namespace fs = std::filesystem;
    if (const char* e = std::getenv("AIVERSE_ENGINE_DIR"); e && *e && looksLikeEngineDir(e)) return e;
    if (looksLikeEngineDir(compiled)) return compiled;
    fs::path d = hostExecutablePath(argv0).parent_path();
    for (int up = 0; up < 5 && !d.empty(); ++up, d = d.parent_path()) {   // bin/, App.app/Contents/MacOS/, ...
        if (looksLikeEngineDir(d)) return d.lexically_normal().string();
        if (d == d.parent_path()) break;
    }
    return compiled;
}

} // namespace aiverse::platform
