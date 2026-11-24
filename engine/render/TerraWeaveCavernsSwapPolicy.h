#pragma once
// Stage24: pure, toolchain-independent revision and budget guards for
// staged GeometryScene ownership. No RHI includes; safe to compile in isolation.
#include <cstddef>
#include <cstdint>
namespace aiverse::terraweave::caverns {
inline bool caveCanStageScene(uint64_t active,uint64_t staged,
                             uint64_t proposed,size_t liveOwners,
                             size_t maxOwners) noexcept {
    return proposed>0 && proposed>active && proposed>staged &&
           maxOwners>0 && liveOwners<maxOwners;
}
inline bool caveCanActivateScene(uint64_t active,uint64_t staged) noexcept {
    return staged>0 && staged>active;
}
} // namespace aiverse::terraweave::caverns
