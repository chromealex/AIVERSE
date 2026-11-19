// TerraWeave: genre-independent canopy appearance policy.
// Presentation-only. Caller owns seasons, climate and authoritative simulation.
// Integer permille inputs permit deterministic CPU atlas thinning across hosts.
#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace aiverse::terraweave {

struct CanopyPhenology {
    uint32_t broadleafCoverPermille = 1000; // 0 = bare deciduous canopy
    uint32_t evergreenCoverPermille = 1000; // normally less seasonal variation
    uint32_t tintRPermille = 1000;          // 1000 = unchanged foliage colour
    uint32_t tintGPermille = 1000;
    uint32_t tintBPermille = 1000;
    constexpr bool operator==(const CanopyPhenology&) const = default;
};

inline void validateCanopyPhenology(const CanopyPhenology& p) {
    if (p.broadleafCoverPermille > 1000 ||
        p.evergreenCoverPermille > 1000 ||
        p.tintRPermille > 2000 || p.tintGPermille > 2000 ||
        p.tintBPermille > 2000)
        throw std::invalid_argument("TerraWeave canopy phenology");
}

inline uint32_t canopyCoverPermille(const CanopyPhenology& p, bool evergreen) {
    return evergreen ? p.evergreenCoverPermille : p.broadleafCoverPermille;
}

inline uint8_t tintCanopyChannel(uint8_t channel,uint32_t tintPermille) {
    return static_cast<uint8_t>(
        std::min<uint32_t>(255u,(uint32_t(channel)*tintPermille+500u)/1000u));
}

} // namespace aiverse::terraweave
