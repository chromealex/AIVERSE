// TerraWeave: bounded, immutable spatial wind snapshot for presentation.
// A PHYSIS/game/weather host owns simulation and publishes an entire snapshot.
// No genre, renderer, wall-clock or ECS dependency. Coordinates are world cm.
#pragma once
#include "engine/world/TerraWeaveWind.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

struct ForestWindSample {
    int32_t xCmPerSec=0, zCmPerSec=0;
    uint32_t gustPermille=350;
    constexpr bool operator==(const ForestWindSample&) const = default;
};

// Regular node lattice, row-major X-fastest. Outside the rectangle the
// fallback wind is used. A half-cell interior border fades into fallback,
// so published patches never create a visible step at their outer edges.
struct ForestWindField {
    int64_t originXcm=0, originZcm=0;
    int32_t spacingCm=6400;      // nodes, not tile centres
    uint32_t columns=0, rows=0;  // 0/0 means empty, otherwise 2..64 each
    std::vector<ForestWindSample> samples;

    void validate() const {
        if(columns==0 && rows==0 && samples.empty())return;
        if(columns<2 || columns>64 || rows<2 || rows>64 ||
           samples.size()!=size_t(columns)*rows ||
           spacingCm<100 || spacingCm>10000000 ||
           originXcm < -kWorldLimitCm || originXcm>kWorldLimitCm ||
           originZcm < -kWorldLimitCm || originZcm>kWorldLimitCm ||
           originXcm+int64_t(columns-1)*spacingCm>kWorldLimitCm ||
           originZcm+int64_t(rows-1)*spacingCm>kWorldLimitCm)
            throw std::invalid_argument("TerraWeave wind field lattice");
        for(const auto& v:samples) {
            ForestWindState state;
            state.xCmPerSec=v.xCmPerSec;
            state.zCmPerSec=v.zCmPerSec;
            state.gustPermille=v.gustPermille;
            validateForestWind(state);
        }
    }
    bool anyWind() const {
        for(const auto& p:samples)
            if(p.xCmPerSec!=0 || p.zCmPerSec!=0)return true;
        return false;
    }
};

// Source state controls cycle, branch flex and lean cap. Only the velocity
// and gust fields vary spatially. This can be called per tree on the owning
// render thread; callers validate the immutable field once when publishing it.
inline ForestWindState sampleForestWindField(
        const ForestWindField* field,int64_t xCm,int64_t zCm,
        ForestWindState fallback) {
    if(!field || field->columns==0)return fallback;
    const int64_t x0=field->originXcm,z0=field->originZcm;
    const int64_t x1=x0+int64_t(field->columns-1)*field->spacingCm;
    const int64_t z1=z0+int64_t(field->rows-1)*field->spacingCm;
    if(xCm<x0 || xCm>x1 || zCm<z0 || zCm>z1)return fallback;
    const int64_t dx=xCm-x0,dz=zCm-z0;
    const auto ix=static_cast<uint32_t>(std::min<int64_t>(
        dx/field->spacingCm,field->columns-2));
    const auto iz=static_cast<uint32_t>(std::min<int64_t>(
        dz/field->spacingCm,field->rows-2));
    constexpr int64_t Q=65536;
    const int64_t u=(dx-int64_t(ix)*field->spacingCm)*Q/field->spacingCm;
    const int64_t v=(dz-int64_t(iz)*field->spacingCm)*Q/field->spacingCm;
    const auto& a=field->samples[size_t(iz)*field->columns+ix];
    const auto& b=field->samples[size_t(iz)*field->columns+ix+1];
    const auto& c=field->samples[size_t(iz+1)*field->columns+ix];
    const auto& d=field->samples[size_t(iz+1)*field->columns+ix+1];
    const auto lerp=[](int64_t x,int64_t y,int64_t q){
        return x+(y-x)*q/65536;
    };
    const auto bilerp=[&](int64_t aa,int64_t bb,int64_t cc,int64_t dd){
        return lerp(lerp(aa,bb,u),lerp(cc,dd,u),v);
    };
    // The half-cell inward feather creates continuity between published
    // wind patches and a host's fallback field, including negative positions.
    const int64_t border=std::min({dx,dz,x1-xCm,z1-zCm});
    const int64_t feather=std::max<int64_t>(1,field->spacingCm/2);
    const int64_t weight=std::min<int64_t>(Q,border*Q/feather);
    const auto blend=[&](int64_t channel,int64_t base) {
        return lerp(base,channel,weight);
    };
    fallback.xCmPerSec=static_cast<int32_t>(blend(
        bilerp(a.xCmPerSec,b.xCmPerSec,c.xCmPerSec,d.xCmPerSec),
        fallback.xCmPerSec));
    fallback.zCmPerSec=static_cast<int32_t>(blend(
        bilerp(a.zCmPerSec,b.zCmPerSec,c.zCmPerSec,d.zCmPerSec),
        fallback.zCmPerSec));
    fallback.gustPermille=static_cast<uint32_t>(blend(
        bilerp(a.gustPermille,b.gustPermille,c.gustPermille,d.gustPermille),
        fallback.gustPermille));
    return fallback;
}

// Blend a tree's local wind into one macro observer wind as it moves from
// near geometric LOD toward the far statistical Horizon HLOD. No abrupt
// wind direction change when a tree becomes an impostor.
inline ForestWindState blendForestWind(
        ForestWindState local,ForestWindState macro,uint32_t macroPermille) {
    const int64_t weight=std::min<uint32_t>(macroPermille,1000u);
    const auto mix=[&](int64_t a,int64_t b){
        return a+(b-a)*weight/1000;
    };
    local.xCmPerSec=static_cast<int32_t>(
        mix(local.xCmPerSec,macro.xCmPerSec));
    local.zCmPerSec=static_cast<int32_t>(
        mix(local.zCmPerSec,macro.zCmPerSec));
    local.gustPermille=static_cast<uint32_t>(
        mix(local.gustPermille,macro.gustPermille));
    return local;
}

} // namespace aiverse::terraweave
