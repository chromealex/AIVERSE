#pragma once
#include <glib.h>
#include "apps/editor/core/ProjectFile.h"
#include <filesystem>
#include <string>

namespace aiverse::studio {
inline std::filesystem::path studioLastProjectFile() {
    return std::filesystem::path(g_get_user_config_dir()) / "AIVERSE" / "Studio" / "last-project";
}

inline void storeStudioLastProject(const std::string& path) {
    const auto file = studioLastProjectFile();
    if (g_mkdir_with_parents(file.parent_path().c_str(), 0700) != 0) {
        g_warning("Cannot create Studio settings directory");
        return;
    }
    GError* error = nullptr;
    if (!g_file_set_contents_full(file.c_str(), path.data(), path.size(), G_FILE_SET_CONTENTS_CONSISTENT,
                                  0600, &error)) {
        g_warning("Cannot save last Studio project: %s", error->message);
        g_error_free(error);
    }
}

inline std::string loadStudioLastProject() {
    gchar* contents = nullptr;
    gsize length = 0;
    if (!g_file_get_contents(studioLastProjectFile().c_str(), &contents, &length, nullptr)) return {};
    std::string path(contents, length);
    g_free(contents);
    if (path.find('\0') != path.npos) return {};
    return editor::resolveProjectDir(path, nullptr);
}
}
