#pragma once
#include <gio/gio.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::studio {
// Launch from the GTK thread. Completion and polling use the same main context.
inline std::function<std::optional<int>()> launchStudioChild(
    const std::filesystem::path& executable, const std::vector<std::string>& arguments) {
    std::vector<const gchar*> argv{executable.c_str()};
    for (const auto& argument : arguments) argv.push_back(argument.c_str());
    argv.push_back(nullptr);
    GError* error = nullptr;
    auto* child = g_subprocess_newv(argv.data(), G_SUBPROCESS_FLAGS_NONE, &error);
    if (!child) {
        std::string reason = error->message;
        g_error_free(error);
        throw std::runtime_error(reason);
    }
    using Status = std::shared_ptr<std::optional<int>>;
    auto status = std::make_shared<std::optional<int>>();
    g_subprocess_wait_async(child, nullptr, +[](GObject* object, GAsyncResult* result, gpointer data) {
        std::unique_ptr<Status> state(static_cast<Status*>(data));
        auto* process = G_SUBPROCESS(object);
        if (!g_subprocess_wait_finish(process, result, nullptr)) **state = -1;
        else if (g_subprocess_get_if_signaled(process)) **state = 128 + g_subprocess_get_term_sig(process);
        else **state = g_subprocess_get_exit_status(process);
    }, new Status(status));
    g_object_unref(child);
    return [status] { return *status; };
}
}
