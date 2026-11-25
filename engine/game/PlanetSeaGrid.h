// Derived shoreline projection from the canonical NEREID mean sea.
// No second water state. The owner refreshes it at terrain publication and
// after rollback/disk load. Authored solid cells and non-water bits survive.
#pragma once
#include "PlanetSeaVolume.h"
#include "MapGrid.h"

namespace aiverse::game {
inline bool refreshPlanetSeaGrid(const World& world,const Surface& surface,MapGrid& grid){
    using namespace lib::nereid;
    if(grid.w!=surface.W||grid.h!=surface.H||grid.flags.size()!=size_t(grid.w)*size_t(grid.h))return false;
    std::vector<Fixed> radii(surface.bodies.size());
    bool valid=true;
    world.eachExistingConst<PlanetSeaVolumeComponent>([&](Entity e,const PlanetSeaVolumeComponent&){
        if(!world.alive(e))return;
        const auto* sea=world.tryGetConst<WaterSphereComponent>(e);
        const int body=sea?planet_sea_detail::seaBody(surface,*sea):-1;
        if(body<0||radii[size_t(body)]>Fixed{}||sea->surfaceRadius>Fixed::fromInt(1500)){
            valid=false;return;
        }
        radii[size_t(body)]=sea->surfaceRadius;
    });
    if(!valid)return false;
    bool changed=false;
    for(size_t b=0;b<radii.size();++b){
        if(radii[b]<=Fixed{})continue;
        const auto& def=surface.bodies[b];
        for(int f=0;f<6;++f)for(int y=0;y<def.faceSize;++y)for(int x=0;x<def.faceSize;++x){
            const auto c=surface.gridCell(int(b),f,x,y);
            auto& flags=grid.flags[size_t(c)];
            if(!(flags&(kCellGround|kCellWater)))continue;
            const bool wet=def.radius+surface.cellHeight(c)<radii[b];
            const auto next=uint8_t((flags&~(kCellGround|kCellWater))|(wet?kCellWater:kCellGround));
            if(flags!=next){flags=next;changed=true;}
        }
    }
    grid.planetSeaRadii=std::move(radii);
    if(changed)grid.invalidateSphereTerrain();
    return true;
}
inline Vec3 placePlanetNaval(const Surface& surface,const MapGrid& grid,Vec3 pos){
    const int b=surface.bodyAt(pos);
    const auto radius=grid.liveSeaRadius(b);
    if(radius<=Fixed{})return surface.project(pos);
    const auto ground=surface.bodies[size_t(b)].radius+surface.heightAt(pos);
    // A dry/stranded ship stays on the ground, never teleports to water.
    if(ground>=radius)return surface.project(pos);
    // The gameplay probe uses strict submergence. Keep the reference just
    // below the surface, but not below the actual terrain.
    return surface.toShell(pos,fxMax(ground,radius-Fixed::fromRatio(1,256)));
}
} // namespace aiverse::game
