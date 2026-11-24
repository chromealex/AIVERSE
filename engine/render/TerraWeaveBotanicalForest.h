// Textured botanical species for ArborForestAdapter / TerraWeaveForestScene:
// presets from TerraWeaveTreePresets.h (oak, birch, spruce...), their
// botanical mesh recipe and PBR bark/leaf maps
// (<dir>/<stem>_basecolor|normal|orm.png, as the vegetation-pbr catalog
// package ships them in textures/). Used by the editor forest overlay and the
// WorldScene "terraweaveForest" manifest, so both draw the same trees.
#pragma once

#include "engine/render/Model.h"
#include "engine/world/TerraWeaveBotanicalAppearance.h"
#include "engine/world/TerraWeaveTreePresets.h"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace aiverse::terraweave {

using BotanicalTextureReader = std::function<bool(const std::string& path, std::string& bytes)>;

inline const TreePreset* findBotanicalPreset(std::string_view name) {
    for (const auto& preset : botanicalTreePresets())
        if (preset.name == name) return &preset;
    return nullptr;
}

// Assign one PBR family to material `slot` (0 bark, 1 foliage) of a freshly
// built Arbor model (before GPU upload). Missing files leave the material
// untextured. Foliage becomes an alpha-cutout, double-sided card material.
inline bool applyBotanicalTexture(render::Model& model, size_t slot, const std::string& dir,
                                  const std::string& stem, const BotanicalTextureReader& read) {
    if (slot >= model.materials.size() || !read || stem.empty()) return false;
    const std::string base = (dir.empty() ? std::string() : dir + "/") + stem + "_";
    auto add = [&](const char* suffix) {
        std::string bytes;
        render::Image image;
        if (!read(base + suffix + ".png", bytes) || !render::decodeImage(bytes, image)) return -1;
        image.name = base + suffix + ".png";
        model.images.push_back(std::move(image));
        return int(model.images.size() - 1);
    };
    auto& m = model.materials[slot];
    const int color = add("basecolor");
    if (color < 0) return false;
    m.baseTex = color;
    m.normalTex = add("normal");
    m.mrTex = add("orm");
    m.occlusionTex = m.mrTex;
    m.baseColor = {1, 1, 1, 1};
    m.roughness = 1;
    m.metallic = 0;
    if (slot == 1) {
        m.alpha = render::AlphaMode::Mask;
        m.alphaCutoff = .38f;
        m.doubleSided = true;
    }
    return true;
}

// Per-species bark/leaf stems -> decorator for
// ArborForestAdapter::setPrototypeDecorator.
struct BotanicalMaterialTable {
    std::string dir;
    BotanicalTextureReader read;
    std::unordered_map<uint32_t, std::pair<std::string, std::string>> stems;

    void add(uint32_t speciesId, const TreePreset& preset) {
        stems[speciesId] = {std::string(preset.barkMaterial), std::string(preset.leafMaterial)};
    }
    std::function<void(uint32_t, render::Model&)> decorator() const {
        auto self = std::make_shared<BotanicalMaterialTable>(*this);
        return [self](uint32_t id, render::Model& model) {
            const auto found = self->stems.find(id);
            if (found == self->stems.end()) return;
            applyBotanicalTexture(model, 0, self->dir, found->second.first, self->read);
            applyBotanicalTexture(model, 1, self->dir, found->second.second, self->read);
        };
    }
};

} // namespace aiverse::terraweave
