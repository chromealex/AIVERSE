#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace aiverse::studio {
struct StudioWebAsset {
    std::filesystem::path file;
    const char* mime;
    static constexpr std::uintmax_t maxBytes = 32u * 1024u * 1024u;
};

// The URI adapter decodes the URL path once before calling this shared resolver.
inline std::optional<StudioWebAsset> studioWebAsset(const std::filesystem::path& root, std::string_view path) {
    namespace fs = std::filesystem;
    if (path.empty() || path.front() != '/' || path.find('\\') != path.npos || path.find('\0') != path.npos) return {};
    if (path == "/") path = "/index.html";
    fs::path relative(path.substr(1));
    if (relative.has_root_path()) return {};
    for (const auto& part : relative) if (part == "." || part == "..") return {};
    std::error_code ec;
    const auto base = fs::canonical(root, ec);
    if (ec) return {};
    const auto file = fs::canonical(base / relative, ec);
    if (ec) return {};
    const auto inside = file.lexically_relative(base);
    if (inside.empty() || *inside.begin() == ".." || !fs::is_regular_file(file, ec)) return {};
    if (fs::file_size(file, ec) > StudioWebAsset::maxBytes || ec) return {};
    auto ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    static constexpr struct { const char* extension; const char* mime; } types[] = {
        {".html","text/html"},{".js","text/javascript"},{".mjs","text/javascript"},{".css","text/css"},
        {".json","application/json"},{".svg","image/svg+xml"},{".png","image/png"},{".jpg","image/jpeg"},
        {".jpeg","image/jpeg"},{".webp","image/webp"},{".avif","image/avif"},{".gif","image/gif"},
        {".ico","image/x-icon"},{".woff","font/woff"},{".woff2","font/woff2"},{".ttf","font/ttf"},{".wasm","application/wasm"}
    };
    for (const auto& type : types) if (ext == type.extension) return StudioWebAsset{file,type.mime};
    return {};
}
}
