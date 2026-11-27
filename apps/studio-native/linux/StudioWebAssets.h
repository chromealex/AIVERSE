#pragma once
#include "../StudioWebAsset.h"
#include <webkit2/webkit2.h>

namespace aiverse::studio {
// Register once per WebKit context before loading aiverse-studio://shell/index.html.
inline void registerStudioWebAssets(WebKitWebContext* context, const std::filesystem::path& directory) {
    constexpr auto scheme = "aiverse-studio";
    auto* security = webkit_web_context_get_security_manager(context);
    webkit_security_manager_register_uri_scheme_as_secure(security, scheme);
    webkit_security_manager_register_uri_scheme_as_cors_enabled(security, scheme);
    webkit_web_context_register_uri_scheme(context, scheme, [](WebKitURISchemeRequest* request, gpointer data) {
        auto* uri = g_uri_parse(webkit_uri_scheme_request_get_uri(request), G_URI_FLAGS_ENCODED_PATH, nullptr);
        gchar* path = uri ? g_uri_unescape_string(g_uri_get_path(uri), nullptr) : nullptr;
        std::optional<StudioWebAsset> asset;
        if (uri && path && g_strcmp0(g_uri_get_host(uri),"shell")==0 && !g_uri_get_userinfo(uri) && g_uri_get_port(uri)==-1)
            asset = studioWebAsset(*static_cast<const std::filesystem::path*>(data),path);
        g_free(path);
        if (uri) g_uri_unref(uri);
        GError* error = nullptr;
        if (asset) {
            GFile* file = g_file_new_for_path(asset->file.c_str());
            auto* stream = g_file_read(file,nullptr,&error);
            g_object_unref(file);
            if (stream) {
                webkit_uri_scheme_request_finish(request,G_INPUT_STREAM(stream),-1,asset->mime);
                g_object_unref(stream);
                return;
            }
        }
        if (!error) error = g_error_new_literal(G_IO_ERROR,G_IO_ERROR_NOT_FOUND,"Studio web asset unavailable");
        webkit_uri_scheme_request_finish_error(request,error);
        g_error_free(error);
    },new std::filesystem::path(directory),[](gpointer data){ delete static_cast<std::filesystem::path*>(data); });
}
}
