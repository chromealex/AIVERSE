// Where the engine's files are (templates/, engine/assets, sources for rtsbuild) for host tools: the editor,
// rtsbuild, assetpipe. A developer build uses the source tree it was compiled from; a packaged engine
// (e.g. the Windows build: <root>/bin/*.exe next to <root>/templates) is found next to the executable,
// so the folder can be moved or unpacked anywhere. RTS_ENGINE_DIR overrides both.
#pragma once
#include <cstdlib>
#include <filesystem>
#include <string>

namespace rts::platform {

inline bool looksLikeEngineDir(const std::filesystem::path& d) {
    std::error_code ec;
    return std::filesystem::is_directory(d / "templates", ec) && std::filesystem::is_directory(d / "engine" / "assets", ec);
}

inline std::string resolveEngineDir(const std::string& compiled, const char* argv0) {
    namespace fs = std::filesystem;
    if (const char* e = std::getenv("RTS_ENGINE_DIR"); e && *e && looksLikeEngineDir(e)) return e;
    if (looksLikeEngineDir(compiled)) return compiled;
    std::error_code ec;
    fs::path exe = argv0 ? fs::absolute(argv0, ec) : fs::path();
#if defined(__linux__)
    if (fs::path self = fs::read_symlink("/proc/self/exe", ec); !ec) exe = self;
#endif
    fs::path d = exe.parent_path();
    for (int up = 0; up < 5 && !d.empty(); ++up, d = d.parent_path()) {   // bin/, App.app/Contents/MacOS/, ...
        if (looksLikeEngineDir(d)) return d.lexically_normal().string();
        if (d == d.parent_path()) break;
    }
    return compiled;
}

} // namespace rts::platform
