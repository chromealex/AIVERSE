#pragma once
#include <algorithm>
#include <array>

namespace aiverse::studio {
using StudioMapCell = std::array<int,2>;
using StudioMapAreaBounds = std::array<int,4>;

inline StudioMapAreaBounds studioAreaBetween(StudioMapCell a, StudioMapCell b) {
    const auto [left,right]=std::minmax(a[0],b[0]);
    const auto [top,bottom]=std::minmax(a[1],b[1]);
    return {left,top,right-left+1,bottom-top+1};
}
}
