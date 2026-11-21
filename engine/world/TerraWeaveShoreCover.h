// Shared native authoring mask for PBR sand/wetness and grass exclusions.
// Samples NEREID footprint clearance, never a second water or terrain solver.
#pragma once
#include "engine/core/Fixed.h"
#include <stdexcept>

namespace aiverse::terraweave {
struct ShoreCover {
    uint32_t sandPermille=0,wetPermille=0;
};
inline Fixed shoreSmooth(Fixed from,Fixed to,Fixed value) {
    const auto t=fxClamp((value-from)/(to-from),0_fx,1_fx);
    return t*t*(3_fx-2_fx*t);
}
inline ShoreCover shoreCover(Fixed outsideDistance,Fixed heightAboveWater,Fixed bankWidth=6_fx) {
    if(bankWidth<1_fx||bankWidth>100_fx)throw std::invalid_argument("Terra shore width1..100m");
    const auto horizontal=1_fx-shoreSmooth(0_fx,bankWidth,fxMax(outsideDistance,0_fx));
    const auto sand=horizontal*(1_fx-shoreSmooth(Fixed::fromRatio(35,100),Fixed::fromRatio(25,10),heightAboveWater));
    const auto wet=horizontal*(1_fx-shoreSmooth(Fixed::fromRatio(5,100),Fixed::fromRatio(45,100),heightAboveWater));
    return {uint32_t((fxClamp(sand,0_fx,1_fx)*1000_fx).floorInt()),
            uint32_t((fxClamp(wet,0_fx,1_fx)*1000_fx).floorInt())};
}
} // namespace aiverse::terraweave
