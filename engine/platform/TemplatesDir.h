// Where the sample games (project templates) are for host tools. They are packages of the package catalog
// (AIVERSE-PACKAGES), installed by tools/pkg/fetch_templates.py into one folder named by engine id:
// AIVERSE_TEMPLATES_DIR, else <packages home>/templates (AIVERSE_PACKAGES_HOME or ~/.aiverse/packages).
// An engine folder that still carries templates/ (older packaged builds) keeps using it; a developer build
// prefers the folder CMake installed into.
#pragma once
#include <cstdlib>
#include <filesystem>
#include <string>

namespace aiverse::platform {

inline std::string templatesDir(const std::string& engineDir) {
    namespace fs = std::filesystem;
    if (const char* e = std::getenv("AIVERSE_TEMPLATES_DIR"); e && *e) return e;
    std::error_code ec;
    if (!engineDir.empty() && fs::is_directory(fs::path(engineDir) / "templates", ec)) return (fs::path(engineDir) / "templates").string();
#ifdef AIVERSE_TEMPLATES_DIR   // the folder this build was configured with (CMake AIVERSE_TEMPLATES_DIR)
    if (fs::is_directory(AIVERSE_TEMPLATES_DIR, ec)) return AIVERSE_TEMPLATES_DIR;
#endif
    if (const char* h = std::getenv("AIVERSE_PACKAGES_HOME"); h && *h) return (fs::path(h) / "templates").string();
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return (fs::path(home && *home ? home : ".") / ".aiverse" / "packages" / "templates").string();
}

} // namespace aiverse::platform
