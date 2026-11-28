#pragma once
#include "apps/editor/core/BrandingOps.h"
#include "engine/data/Json.h"
#include "apps/editor/core/ProjectFileEdits.h"
#include "engine/atlas/AtlasFormat.h"
#include "engine/render/TextureCache.h"
#include "engine/world/WorldAssets.h"
#include <map>
#include <optional>

namespace aiverse::studio {

inline bool readStudioOverlayAsset(const std::filesystem::path& project, const std::string& path, std::string& bytes) {
    namespace fs = std::filesystem;
    if (!world::safeAssetPath(path)) return false;
    const auto root = fs::weakly_canonical(project);
    const auto file = fs::weakly_canonical(root / path);
    const auto local = file.lexically_relative(root);
    if (local.empty() || *local.begin() == ".." || !fs::is_regular_file(file)) return false;
    bytes = editor::readProjectFileBytes(file);
    return true;
}

using StudioPngEncoder = std::string (*)(const std::vector<uint8_t>&, int, int);

inline void studioOverlayImages(const std::string& project, const Json& items, Json& result, StudioPngEncoder encode) {
    Json screen = Json::array(), worldIcons = Json::array(), textures = Json::object(), missing = Json::array();
    const auto read = [&](const std::string& path, std::string& bytes) { return readStudioOverlayAsset(project,path,bytes); };
    const auto decode = [](const std::string& bytes, atlas::Image& image) {
        return render::decodeImage(bytes,image.width,image.height,image.rgba);
    };
    std::optional<atlas::ImageSource> atlasSource;
    std::map<std::string, atlas::Image> sources;
    for (const auto& item : items.items()) {
        if (item.getStr("shape") != "icon") { screen.push(item); continue; }
        const auto path = item.getStr("image");
        auto [it, inserted] = sources.try_emplace(path);
        auto& source = it->second;
        if (inserted) {
            if (!atlasSource) atlasSource.emplace(read, decode);
            if (!atlasSource->load(path,source)) {
                std::string bytes;
                if (!read(path,bytes) || !decode(bytes,source)) {
                    source = {};
                    missing.push(Json::string(path));
                }
            }
        }
        if (source.empty()) continue;
        const uint32_t tint = uint32_t(item.getInt("color"));
        const auto key = path + ":" + std::to_string(tint);
        if (!textures.has(key)) {
            auto pixels = source.rgba;
            for (size_t i = 0; i < pixels.size(); ++i)
                pixels[i] = uint8_t((pixels[i] * ((tint >> (8 * (i % 4))) & 255) + 127) / 255);
            textures.set(key, Json::string("data:image/png;base64," +
                editor::base64Encode(encode(pixels,source.width,source.height))));
        }
        Json icon = item;
        icon.set("texture", Json::string(key));
        (item.has("before") ? worldIcons : screen).push(std::move(icon));
    }
    result.set("screen", std::move(screen));
    result.set("worldIcons", std::move(worldIcons));
    result.set("textures", std::move(textures));
    result.set("missingImages", std::move(missing));
}

} // namespace aiverse::studio
