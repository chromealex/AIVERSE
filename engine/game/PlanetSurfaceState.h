// GameCore is the sole owner of the live spherical Surface. No soil store.
#pragma once
#include "GameState.h"
#include "engine/core/Hash.h"

namespace aiverse::game {
inline uint64_t planetSurfaceRecipeHash(const Surface& s){
    Hasher h;h.u32(uint32_t(s.bodies.size()));h.i32(s.startBody);
    for(const auto& b:s.bodies){
        h.str(b.id);h.u32(uint32_t(b.kind));h.i32(b.faceSize);h.fx(b.radius);
        h.fx(b.seaLevel);h.fx(b.orbitAltitude);h.i32(b.parent);
        h.fx(b.orbitRadius);h.fx(b.orbitPeriod);h.fx(b.orbitPhase);h.v3(b.pos);
    }
    h.u64(uint64_t(s.corners.size()));
    for(auto v:s.corners)h.i32(v);
    return h.value();
}
inline bool validPlanetSurfaceCorners(const Surface& source,const std::vector<int16_t>& corners){
    if(!source.tab||corners.size()!=source.corners.size())return false;
    for(size_t b=0;b<source.bodies.size();++b){
        const auto& body=source.bodies[b];if(!body.hasSurface())continue;
        const int n=body.faceSize;const auto& cube=source.cubeOf(int(b));
        for(int f=0;f<6;++f)for(int y=0;y<=n;++y)for(int x=0;x<=n;++x){
            const auto id=body.cornerBase+size_t(cube.cornerId(f,x,y));
            if(id>=corners.size())return false;
            const auto height=corners[id];
            const auto radius=body.radius+Fixed::fromRatio(height,16);
            if(radius<=Fixed{}||radius>Fixed::fromInt(1500))return false;
            if(x==0||x==n||y==0||y==n){
                int aliases[3];const int count=cube.cornerDuplicates(f,x,y,aliases);
                for(int i=0;i<count;++i){
                    const auto alias=body.cornerBase+size_t(aliases[i]);
                    if(alias>=corners.size()||corners[alias]!=height)return false;
                }
            }
        }
    }
    return true;
}
inline void bindPlanetSurface(GameState& gs,std::shared_ptr<const Surface> surface,uint64_t revision){
    gs.surface=surface;gs.grid.surf=surface;gs.hash.surf=std::move(surface);
    gs.planetTerrainRevision=revision;
}
} // namespace aiverse::game
