// Shared slot reservation for dynamic GI and textured Shader Graph materials.
// The 13 pre-existing sampled texture bindings remain untouched. Dynamic GI
// uses contiguous tex13.. and Shader Graph starts immediately AFTER the
// selected GI permutation. An unsupported GLES/Vulkan sampler budget falls
// back to the old Shader Graph-only material instead of colliding with tex13.
#pragma once
#include "GiTextureBudget.h"
#include <cstddef>
#include <limits>

namespace aiverse::render {
inline constexpr int kGiGraphBaseTextureSlot=13;

constexpr int giGraphTextureStart(bool dynamicGi,bool validity=false,
                                  bool farCascade=false,bool thirdCascade=false) {
    if(!dynamicGi)return kGiGraphBaseTextureSlot;
    return giFragmentTextureSlotsRequired(validity,farCascade,thirdCascade);
}
constexpr bool giGraphTextureSlotsFit(int available,size_t graphTextures,
                                      bool dynamicGi,bool validity=false,
                                      bool farCascade=false,bool thirdCascade=false) {
    const int base=giGraphTextureStart(dynamicGi,validity,farCascade,thirdCascade);
    if(base<=0 || available<base ||
       graphTextures>size_t(std::numeric_limits<int>::max()-base))
        return false;
    return graphTextures<=size_t(available-base);
}
} // namespace aiverse::render
