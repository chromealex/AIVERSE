#pragma once
#include "engine/core/ECS.h"
#include "engine/core/Fixed.h"
#include <array>
#include <cstddef>

namespace aiverse::lib::mechanics {
inline constexpr size_t kMaxExtraWeaponTargets = 16;
struct RankedWeaponTarget { Entity entity = kNullEntity; Fixed distance{}; };

// Hosts validate hostility, geometry and their own target filters before
// submitting a candidate. Bounded storage; stable distance/identity ordering
// independent of the spatial query traversal order.
class NearestWeaponTargets {
public:
    explicit NearestWeaponTargets(size_t limit)
        : limit_(limit > kMaxExtraWeaponTargets ? kMaxExtraWeaponTargets : limit) {}
    void consider(Entity entity, Fixed distance) {
        if (!entity.valid() || !limit_) return;
        for (size_t i=0;i<count_;++i) if (items_[i].entity==entity) return;
        size_t at=0;
        while (at<count_ && (items_[at].distance<distance ||
            (items_[at].distance==distance && items_[at].entity.packed()<entity.packed()))) ++at;
        if (at>=limit_) return;
        if (count_<limit_) ++count_;
        for (size_t i=count_-1;i>at;--i) items_[i]=items_[i-1];
        items_[at]={entity,distance};
    }
    const RankedWeaponTarget* begin() const { return items_.data(); }
    const RankedWeaponTarget* end() const { return items_.data()+count_; }
    size_t size() const { return count_; }
private:
    std::array<RankedWeaponTarget,kMaxExtraWeaponTargets> items_{};
    size_t limit_=0,count_=0;
};
} // namespace aiverse::lib::mechanics
