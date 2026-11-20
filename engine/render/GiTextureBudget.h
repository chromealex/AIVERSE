// RHI-independent fragment texture slot admission for dynamic GI model paths.
// Counts sampled texture *bindings*, not Metal hardware samplers. The RHI
// consumes tex0..12 before GI; Shader Graph has its own tex13+ reservation.
#pragma once
namespace aiverse::render {
constexpr int giFragmentTextureSlotsRequired(bool validity, bool farCascade,
                                             bool thirdCascade) {
    // An invalid combination must never be accepted as fewer bindings.
    if ((farCascade && !validity) || (thirdCascade && !farCascade))
        return 0;
    return thirdCascade ? 17 : farCascade ? 16 : validity ? 15 : 14;
}
constexpr bool giFragmentTextureBudgetAllows(int available, bool validity,
                                             bool farCascade, bool thirdCascade) {
    const int need=giFragmentTextureSlotsRequired(validity,farCascade,thirdCascade);
    return need>0 && available>=need;
}
} // namespace aiverse::render
