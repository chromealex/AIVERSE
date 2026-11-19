// TerraWeave: PHYSIS-neutral, bounded cosmetic tree sway policy.
// World/environment gameplay provides signed wind and an authoritative tick;
// nothing here is a force, collision change or physics simulation.
#pragma once

#include "engine/world/TerraWeave.h"
#include <cstdint>
#include <stdexcept>

namespace aiverse::terraweave {

struct ForestWindState {
    int32_t xCmPerSec = 0, zCmPerSec = 0; // horizontal world-space wind
    uint32_t gustPermille = 350;          // 0..1000 modulation around base lean
    uint32_t cycleTicks = 120;            // stable, per-tree staggered sway
    uint32_t maxLeanMilliDegrees = 1500;  // max angle at reference 50m/s
    uint32_t branchFlexPermille = 600;    // 0=root lean only; 1..1000=branch pose
    constexpr bool operator==(const ForestWindState&) const = default;
};

inline void validateForestWind(const ForestWindState& state) {
    constexpr int64_t kMaxWindCmPerSec=5000;
    const int64_t x=state.xCmPerSec, z=state.zCmPerSec;
    if(x < -kMaxWindCmPerSec || x > kMaxWindCmPerSec ||
       z < -kMaxWindCmPerSec || z > kMaxWindCmPerSec ||
       x*x+z*z>kMaxWindCmPerSec*kMaxWindCmPerSec ||
       state.gustPermille>1000 || state.cycleTicks<8 ||
       state.cycleTicks>4096 || state.maxLeanMilliDegrees>3000 ||
       state.branchFlexPermille>1000)
        throw std::invalid_argument("TerraWeave wind presentation");
}

struct ForestLeanMilliDegrees {
    int32_t towardX=0,towardZ=0;
    constexpr bool operator==(const ForestLeanMilliDegrees&) const = default;
};

// Stable piecewise-linear oscillation in [-1000,1000] from an integer tick.
// Per-tree phase prevents entire forests oscillating in lockstep.
inline ForestLeanMilliDegrees forestTreeLean(
        const ForestWindState& wind,uint64_t tick,uint64_t treeKey) {
    if((wind.xCmPerSec==0 && wind.zCmPerSec==0) ||
       wind.maxLeanMilliDegrees==0)return {};
    const uint64_t cycle=wind.cycleTicks;
    const uint64_t phase=(tick%cycle+
        (detail::mix64(treeKey ^ 0x57494e4453574159ULL)%cycle))%cycle;
    const int64_t triangle=(phase*2u<cycle) ?
        -1000+int64_t(4000u*phase/cycle) :
         3000-int64_t(4000u*phase/cycle);
    const int64_t strength=1000+
        triangle*int64_t(wind.gustPermille)/1000;
    const auto angle=[&](int32_t component)->int32_t {
        const int64_t result=int64_t(wind.maxLeanMilliDegrees)*
            component*strength/(5000LL*1000LL);
        return static_cast<int32_t>(result);
    };
    return {angle(wind.xCmPerSec),angle(wind.zCmPerSec)};
}

} // namespace aiverse::terraweave
