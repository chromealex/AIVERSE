// Cosmetic rock placement. Integer descriptors; no gameplay/physics authority.
#pragma once
#include "engine/world/TerraWeave.h"
#include <array>
#include <functional>
#include <optional>

namespace aiverse::terraweave {
struct RockScatterSettings {
    uint64_t seed=1;
    int32_t spacingCm=800, radiusMinCm=35, radiusMaxCm=180;
    uint32_t densityPermille=450, variants=8, maxSlopePermille=800;
    uint32_t burialPermille=180;
};
struct RockInstance {
    uint64_t key=0;
    uint32_t variant=0, yawMilliDegrees=0;
    int64_t xCm=0,zCm=0;
    int32_t groundCm=0,radiusCm=0;
    uint32_t burialPermille=0;
    constexpr bool operator==(const RockInstance&) const = default;
};
using RockHeightSource=std::function<std::optional<int32_t>(int64_t,int64_t)>;
using RockPlacementFilter=std::function<bool(const RockInstance&)>;

// The low two bits color the global lattice; all eight immediately neighboring
// cells use different prototypes, including across negative/chunk boundaries.
inline uint32_t rockVariant(int64_t cx,int64_t cz,uint64_t seed,uint32_t count) {
    if(count<4 || count>64 || count%4)throw std::invalid_argument("rock variants must be 4..64, multiple of four");
    const uint32_t color=uint32_t(uint64_t(cx)&1u)+2u*uint32_t(uint64_t(cz)&1u);
    return color+4u*uint32_t(detail::hash2(cx,cz,seed)%(count/4));
}
inline std::vector<RockInstance> scatterRocks(RectCm bounds,const RockScatterSettings& s,
        const RockHeightSource& height,const RockPlacementFilter& accept={}) {
    if(!height || s.spacingCm<1 || s.spacingCm>1000000 || s.radiusMinCm<1 ||
       s.radiusMaxCm<s.radiusMinCm || s.radiusMaxCm>10000 ||
       s.densityPermille>1000 || s.burialPermille>750 || s.maxSlopePermille>10000 ||
       bounds.minX>bounds.maxX || bounds.minZ>bounds.maxZ)
        throw std::invalid_argument("TerraWeave rock scatter settings");
    (void)rockVariant(0,0,s.seed,s.variants);
    if(bounds.minX < -kWorldLimitCm+s.radiusMaxCm || bounds.minZ < -kWorldLimitCm+s.radiusMaxCm ||
       bounds.maxX > kWorldLimitCm-s.radiusMaxCm || bounds.maxZ > kWorldLimitCm-s.radiusMaxCm)
        throw std::out_of_range("TerraWeave rock world extent");
    if(bounds.minX==bounds.maxX || bounds.minZ==bounds.maxZ || !s.densityPermille)return {};
    const auto x0=detail::floorDiv(bounds.minX,s.spacingCm),x1=detail::floorDiv(bounds.maxX-1,s.spacingCm);
    const auto z0=detail::floorDiv(bounds.minZ,s.spacingCm),z1=detail::floorDiv(bounds.maxZ-1,s.spacingCm);
    if(x1-x0>1000000 || z1-z0>1000000 || (x1-x0+1)*(z1-z0+1)>1000000)
        throw std::length_error("TerraWeave rock candidate budget");
    std::vector<RockInstance> out;
    for(int64_t cz=z0;cz<=z1;++cz)for(int64_t cx=x0;cx<=x1;++cx) {
        const uint64_t h=detail::hash2(cx,cz,s.seed);
        const int64_t x=cx*s.spacingCm+int64_t(s.spacingCm)*(250+int64_t(h&65535)*500/65535)/1000;
        const int64_t z=cz*s.spacingCm+int64_t(s.spacingCm)*(250+int64_t((h>>16)&65535)*500/65535)/1000;
        if(!bounds.contains(x,z) || detail::mix64(h)%1000>=s.densityPermille)continue;
        RockInstance at;
        at.key=h;at.xCm=x;at.zCm=z;at.variant=rockVariant(cx,cz,s.seed,s.variants);
        at.yawMilliDegrees=uint32_t(detail::mix64(h^0x9051)%360000);
        at.radiusCm=s.radiusMinCm+int32_t(detail::mix64(h^0xa73) % uint64_t(s.radiusMaxCm-s.radiusMinCm+1));
        at.burialPermille=s.burialPermille;
        int32_t low=INT32_MAX,high=INT32_MIN;bool missing=false;
        // Entire conservative footprint: use the lowest support sample, so a
        // protruding centre on a slope cannot leave the downhill edge floating.
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx) {
            const auto y=height(x+int64_t(dx)*at.radiusCm,z+int64_t(dz)*at.radiusCm);
            if(!y){missing=true;continue;}
            low=std::min(low,*y);high=std::max(high,*y);
        }
        if(missing || (int64_t(high)-low)*1000>int64_t(s.maxSlopePermille)*at.radiusCm*2)continue;
        at.groundCm=low;
        if(!accept || accept(at))out.push_back(at);
    }
    return out;
}
} // namespace aiverse::terraweave
