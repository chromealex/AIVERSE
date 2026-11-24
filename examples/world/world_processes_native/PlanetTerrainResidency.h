// Bounded graph-based residency on the canonical CubeSphere. No planar
// rectangles or duplicate spatial index; neighbors cross face seams.
#pragma once
#include "engine/game/Surface.h"
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <vector>

namespace aiverse::examples::world_processes_native {
struct PlanetTerrainRegion {
    Vec3 position{}; // canonical simulation coordinate, including body anchor
    int cellRings=1; // graph distance, owner includes swept extent and margin
};
struct PlanetTerrainTileKey {
    int face=0,x=0,y=0;
    auto operator<=>(const PlanetTerrainTileKey&)const=default;
};
struct PlanetTerrainCollisionConfig {
    bool enabled=false;
    bool streamed=false;
    size_t maxResidentTiles=256;
    std::vector<PlanetTerrainRegion> regions;
    static constexpr int maxFaceSize=96; // legacy full-body mode
    static constexpr int maxStreamedFaceSize=512; // canonical GameCore limit
    static constexpr int tileSize=16;
    static constexpr int maxRings=64;
    static constexpr size_t maxRegions=1024,maxVisits=262144,maxTiles=1024;
};
inline int planetTerrainCell(const game::Surface& surface,int body,Vec3 position){
    if(!surface.tab||body<0||size_t(body)>=surface.bodies.size()||
       !game::Surface::onSphere(position)||surface.bodyAt(position)!=body)return -1;
    const auto local=position-game::Surface::anchor(body);
    const auto limit=Fixed::fromInt(1500);
    if(local==Vec3{}||fxAbs(local.x)>limit||fxAbs(local.y)>limit||fxAbs(local.z)>limit)return -1;
    return surface.cellAt(position);
}
inline std::optional<PlanetTerrainTileKey> planetTerrainTile(const game::Surface& surface,int body,int cell){
    int b=-1,f=-1,x=-1,y=-1;
    if(cell<0||int64_t(cell)>=int64_t(surface.W)*surface.H||
       !surface.cellFace(cell,b,f,x,y)||b!=body)return {};
    constexpr int tile=PlanetTerrainCollisionConfig::tileSize;
    return PlanetTerrainTileKey{f,x/tile*tile,y/tile*tile};
}
inline std::optional<std::set<PlanetTerrainTileKey>> planPlanetTerrainTiles(
        const game::Surface& surface,int body,const PlanetTerrainCollisionConfig& config){
    using C=PlanetTerrainCollisionConfig;
    if(!surface.tab||body<0||size_t(body)>=surface.bodies.size()||
       !config.maxResidentTiles||config.maxResidentTiles>C::maxTiles)return {};
    const int n=surface.bodies[size_t(body)].faceSize;
    if(n<2||n>(config.streamed?C::maxStreamedFaceSize:C::maxFaceSize))return {};
    std::set<PlanetTerrainTileKey> tiles;
    if(!config.streamed){
        if(!config.regions.empty())return {};
        for(int f=0;f<6;++f)for(int x=0;x<n;x+=C::tileSize)for(int y=0;y<n;y+=C::tileSize)
            tiles.insert({f,x,y});
        if(tiles.size()>config.maxResidentTiles)return {};
        return tiles;
    }
    if(config.regions.empty()||config.regions.size()>C::maxRegions)return {};
    // Largest remaining radius wins. Ordered priority guarantees a cell is
    // finalized once even with overlapping regions of different sizes.
    std::set<std::pair<int,int>> frontier;
    std::map<int,int> remaining;
    auto enqueue=[&](int cell,int rings){
        auto [it,inserted]=remaining.emplace(cell,rings);
        if(!inserted){
            if(it->second>=rings)return;
            frontier.erase({-it->second,cell});it->second=rings;
        }
        frontier.emplace(-rings,cell);
    };
    for(const auto& region:config.regions){
        const int cell=planetTerrainCell(surface,body,region.position);
        if(cell<0||region.cellRings<1||region.cellRings>C::maxRings)return {};
        enqueue(cell,region.cellRings);
    }
    size_t visits=0;
    while(!frontier.empty()){
        if(++visits>C::maxVisits)return {};
        const auto [negative,cell]=*frontier.begin();frontier.erase(frontier.begin());
        const auto key=planetTerrainTile(surface,body,cell);
        if(!key)return {};
        tiles.insert(*key);
        if(tiles.size()>config.maxResidentTiles)return {};
        if(negative<0)for(int k=0;k<8;++k){
            const int neighbor=surface.neighbor(cell,k);
            if(neighbor>=0)enqueue(neighbor,-negative-1);
        }
        if(remaining.size()>C::maxVisits)return {};
    }
    return tiles;
}
} // namespace aiverse::examples::world_processes_native
