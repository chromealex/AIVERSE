// TerraWeave Horizon wind upload limits; public to any native C++ project.
// Presentation-only policy: does not alter wind, physics, terrain or archives.
#pragma once
#include <cstdint>
#include <stdexcept>

namespace aiverse::terraweave {

struct HorizonWindUploadPolicy {
    // Shared per-update bandwidth across active and outgoing HLOD regions.
    // Zero pauses GPU weather metadata updates, preserving safe fallback.
    uint32_t maxBytesPerUpdate=2u*1024u*1024u;
    // Cap contiguous dirty runs per region before a full-range fallback.
    uint32_t maxSparseRanges=8;
    // Fallback when at least this percentage of clusters changed.
    uint32_t fullFallbackPercent=70;
};

inline void validateHorizonWindUploadPolicy(
        const HorizonWindUploadPolicy& policy) {
    if(policy.maxBytesPerUpdate>2u*1024u*1024u ||
       policy.maxSparseRanges==0 || policy.maxSparseRanges>8 ||
       policy.fullFallbackPercent==0 || policy.fullFallbackPercent>100)
        throw std::invalid_argument("TerraWeave Horizon wind upload policy");
}

} // namespace aiverse::terraweave
