// RTS-specific presentation selection policy, independent from legacy RtsView.
// This adapter translates authored simulation type dimensions into a visual
// picking radius. Do not move Rules or UnitType into engine/view.
#pragma once
#include "../RtsData.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace aiverse::game::view_adapter {

// Preserve the historical RTS visual selection footprint exactly.
inline float selectionRadius(const UnitType& type) noexcept {
    if(type.structure)
        return float(std::max(type.footprintW,type.footprintH))*.62f;
    return float(type.radius.toDouble())+.15f;
}

inline float selectionRadius(const Rules& rules,uint16_t type) {
    if(size_t(type)>=rules.types.size())
        throw std::out_of_range("RTS selection type index");
    return selectionRadius(rules.types[size_t(type)]);
}
} // namespace aiverse::game::view_adapter
