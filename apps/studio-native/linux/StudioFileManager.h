#pragma once
#include <gio/gio.h>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace aiverse::studio {
inline std::string studioFileUri(const std::filesystem::path& path) {
    auto* file = g_file_new_for_path(path.c_str());
    gchar* uri = g_file_get_uri(file);
    std::string result(uri);
    g_free(uri);
    g_object_unref(file);
    return result;
}

inline void revealStudioFile(const std::filesystem::path& path, bool openFolder) {
    if (!openFolder) {
        auto* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
        if (bus) {
            const auto uri = studioFileUri(path);
            const gchar* items[] = {uri.c_str(), nullptr};
            auto* result = g_dbus_connection_call_sync(bus, "org.freedesktop.FileManager1",
                "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1", "ShowItems",
                g_variant_new("(^ass)", items, ""), G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NONE,
                1500, nullptr, nullptr);
            g_object_unref(bus);
            if (result) { g_variant_unref(result); return; }
        }
    }
    const auto folder = openFolder ? path : std::filesystem::absolute(path).parent_path();
    const auto uri = studioFileUri(folder);
    GError* error = nullptr;
    if (!g_app_info_launch_default_for_uri(uri.c_str(), nullptr, &error)) {
        std::string message = error->message;
        g_error_free(error);
        throw std::runtime_error(message);
    }
}
}
