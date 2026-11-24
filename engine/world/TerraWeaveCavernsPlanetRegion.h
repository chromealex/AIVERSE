#pragma once
#include "engine/core/Fixed.h"
#include "engine/world/TerraWeaveCavernsVolumeSnapshot.h"
#include <string>
namespace aiverse::terraweave::caverns {
struct CavePlanetAnchor {
    std::string bodyId;
    int32_t face=0;
    Fixed a{},b{};
    int32_t halfExtentCm=800;
};
struct CavePlanetRegionResource {
    uint32_t version=1;
    CavePlanetAnchor anchor;
};
inline void validateCavePlanetBounds(const CavePlanetAnchor& anchor,const CaveVolumeSnapshot& snapshot){
    const int64_t cap=anchor.halfExtentCm;
    if(anchor.bodyId.empty()||anchor.bodyId.size()>128||anchor.face<0||anchor.face>=6||
       anchor.a<Fixed{}||anchor.b<Fixed{}||cap<1||cap>100000||
       snapshot.settings.minYcm< -cap||snapshot.settings.maxYcm>cap)
        throw std::invalid_argument("Caverns planetary chart/vertical recipe extent");
    const auto inside=[&](PointCm p,int64_t r){
        return r>0&&p.x>=-cap+r&&p.x<=cap-r&&p.y>=-cap+r&&p.y<=cap-r&&p.z>=-cap+r&&p.z<=cap-r;
    };
    for(const auto& s:snapshot.stamps)if(!inside(s.center,s.radiusCm))
        throw std::invalid_argument("Caverns planetary stamp exceeds chart");
    for(const auto& e:snapshot.entrances)
        if(!inside({e.xCm,e.caveYcm,e.zCm},e.radiusCm)||!inside({e.xCm,e.surfaceYcm,e.zCm},e.radiusCm))
            throw std::invalid_argument("Caverns planetary entrance exceeds chart");
}
} // namespace aiverse::terraweave::caverns
