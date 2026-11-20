// Genre-independent GI binding contract for custom PBR shaders (terrain,
// foliage, water, user shader pipelines). The caller owns shader compilation
// and declares U6/tex13 and optional U7/tex14/15, Metal U8/tex16 exactly as
// defined in GiDynamicShaders.h + GiCascadeShaders.h.
#pragma once
#include "GiTextureBudget.h"
#include <cstdint>

namespace aiverse::render {

enum class GiCustomShaderMode : uint8_t {
    Off = 0, Single = 1, Validity = 2, DualCascade = 3, TripleCascade = 4
};
constexpr bool giCustomModeUsesValidity(GiCustomShaderMode mode) {
    return mode>=GiCustomShaderMode::Validity;
}
constexpr bool giCustomModeUsesFar(GiCustomShaderMode mode) {
    return mode>=GiCustomShaderMode::DualCascade;
}
constexpr bool giCustomModeUsesThird(GiCustomShaderMode mode) {
    return mode>=GiCustomShaderMode::TripleCascade;
}
constexpr int giCustomModeTextureCount(GiCustomShaderMode mode) {
    switch(mode) {
        case GiCustomShaderMode::Single: return 1;
        case GiCustomShaderMode::Validity: return 2;
        case GiCustomShaderMode::DualCascade: return 3;
        case GiCustomShaderMode::TripleCascade: return 4;
        default: return 0;
    }
}
constexpr int giCustomModeTextureSlots(GiCustomShaderMode mode) {
    return mode==GiCustomShaderMode::Off ? 0 :
           giFragmentTextureSlotsRequired(giCustomModeUsesValidity(mode),
                                          giCustomModeUsesFar(mode),
                                          giCustomModeUsesThird(mode));
}
// Custom renderers already occupying tex13+ must choose an explicit free
// contiguous range, e.g. a terrain pass using tex0..20 chooses firstSlot=21.
// Check subtraction instead of firstSlot+count to avoid integer overflow.
constexpr bool giCustomModeFitsAt(GiCustomShaderMode mode,int firstSlot,
                                  int fragmentSlots,bool metal) {
    if(mode==GiCustomShaderMode::TripleCascade && !metal)return false;
    const int count=giCustomModeTextureCount(mode);
    return count>0 && firstSlot>=0 && fragmentSlots>=firstSlot &&
           count<=fragmentSlots-firstSlot;
}
constexpr bool giCustomModeFits(GiCustomShaderMode mode,int fragmentSlots,
                                bool metal) {
    return giCustomModeFitsAt(mode,13,fragmentSlots,metal);
}
} // namespace aiverse::render
