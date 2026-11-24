// Catalog packages (AIVERSE-PACKAGES) on this machine: where tools/pkg/aiverse_pkg.py keeps them.
//
// Host tools and the editor only (it reads the user's home folder); a game never reads the cache:
// packages are installed into the project (aiverse_pkg.py add) and ship inside its content pack.
// Layout: <home>/<id>/<MAJOR.MINOR.PATCH>/aiverse.package.json + payload, home =
// $AIVERSE_PACKAGES_HOME or ~/.aiverse/packages (%USERPROFILE% on Windows).
#pragma once
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace aiverse::pack {

inline std::filesystem::path packagesHome() {
    if (const char* env = std::getenv("AIVERSE_PACKAGES_HOME"); env && *env) return env;
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home || !*home) return {};
    return std::filesystem::path(home) / ".aiverse" / "packages";
}

// "1.2.3" -> {1,2,3}; false for anything else.
inline bool parsePackageVersion(const std::string& text, std::array<int, 3>& out) {
    int parts = 0;
    size_t i = 0;
    while (parts < 3) {
        if (i >= text.size() || text[i] < '0' || text[i] > '9') return false;
        int value = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            value = value * 10 + (text[i] - '0');
            if (value > 1000000) return false;
            ++i;
        }
        out[parts++] = value;
        if (parts < 3) {
            if (i >= text.size() || text[i] != '.') return false;
            ++i;
        }
    }
    return i == text.size();
}

// Newest cached version of a package that has its manifest, or an empty path. Does not download:
// `python3 tools/pkg/aiverse_pkg.py fetch <id>` does.
inline std::filesystem::path cachedPackageDir(const std::string& id, const std::filesystem::path& home = packagesHome()) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = home / id;
    if (home.empty() || !fs::is_directory(root, ec)) return {};
    fs::path best;
    std::array<int, 3> bestVersion{-1, -1, -1};
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::array<int, 3> v{};
        if (!it->is_directory(ec) || !parsePackageVersion(it->path().filename().string(), v)) continue;
        if (!fs::is_regular_file(it->path() / "aiverse.package.json", ec)) continue;
        if (v > bestVersion) {
            bestVersion = v;
            best = it->path();
        }
    }
    return best;
}

}  // namespace aiverse::pack
