#pragma once
#include "engine/gfx/Gfx.h"
#include <cstdint>
#include <string_view>

namespace aiverse::render {
enum class AlphaMode : uint8_t { Opaque, Mask, Blend, Additive, AdditiveUnweighted };
inline bool alphaBlended(AlphaMode mode) {
    return mode==AlphaMode::Blend || mode==AlphaMode::Additive || mode==AlphaMode::AdditiveUnweighted;
}
inline gfx::BlendMode materialBlendMode(AlphaMode mode) {
    if(mode==AlphaMode::Additive)return gfx::BlendMode::Additive;
    if(mode==AlphaMode::AdditiveUnweighted)return gfx::BlendMode::AdditiveUnweighted;
    return alphaBlended(mode)?gfx::BlendMode::Alpha:gfx::BlendMode::Opaque;
}

inline bool parseMaterialBlend(std::string_view name,AlphaMode& mode) {
    if(name=="opaque")mode=AlphaMode::Opaque;
    else if(name=="mask")mode=AlphaMode::Mask;
    else if(name=="blend" || name=="alpha")mode=AlphaMode::Blend;
    else if(name=="additive")mode=AlphaMode::Additive;
    else if(name=="additive-unweighted")mode=AlphaMode::AdditiveUnweighted;
    else return false;
    return true;
}
} // namespace aiverse::render
