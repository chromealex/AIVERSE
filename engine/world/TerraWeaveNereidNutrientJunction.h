// TerraWeave Stage69 — read-only NEREID confluence/cascade nutrient links.
// Does not modify NEREID WaterBody, hydrology, terrain, or graphics.
// Real geometry/topology is checked against the completed ECS snapshot.
// 4m lattice probes locate narrow stream footprints inside 32m nutrient cells.
#pragma once
#include "engine/world/TerraWeaveForestNutrientCycle.h"
#include <aiverselib/water/NereidQueryIndex.h>
#include <aiverselib/water/NereidConfluences.h>
#include <aiverselib/water/NereidCascades.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct NereidNutrientJunctionSettings {
    uint32_t maxLinks=64;
    uint32_t searchRadiusCells=1;   // neighboring 32m cells
    uint32_t samplesPerAxis=8;      // 4m lattice for narrow streams
};
inline void validateNereidNutrientJunctionSettings(
        NereidNutrientJunctionSettings s) {
    if(!s.maxLinks || s.maxLinks>256 || s.searchRadiusCells>2 ||
       (s.samplesPerAxis!=1 && s.samplesPerAxis!=2 &&
        s.samplesPerAxis!=4 && s.samplesPerAxis!=8))
        throw std::invalid_argument("TerraWeave invalid nutrient junction search");
}

struct NereidNutrientCandidate {
    int64_t x=0,z=0; // 32m cell
    uint64_t distanceSquared=0; // to actual inlet/outlet mouth
};

// A body-local candidate is valid only when a 4/8/16/32m lattice point
// inside its 32m nutrient tile is covered by the ACTUAL authored NEREID
// footprint (polygon, river spline, box, disc, ocean).
inline bool nereidBodyTouchesNutrientCell(
        const World& world,const EarthworksLayer& terrain,
        const lib::nereid::WaterQueryIndex& water,Entity waterBody,
        int64_t cellX,int64_t cellZ,
        uint32_t samplesPerAxis) {
    if(!world.alive(waterBody))return false;
    const auto* body=world.tryGetConst<lib::nereid::WaterBodyComponent>(waterBody);
    if(!body || !lib::nereid::validWaterBody(*body))return false;
    const auto* shape=world.tryGetConst<lib::nereid::WaterGeometryComponent>(
        waterBody);
    const int64_t cm=ForestDeadwoodResource::kNutrientCellCm;
    const int64_t edge=kWorldLimitCm-terrain.spacingCm();
    for(uint32_t j=0;j<samplesPerAxis;++j)
        for(uint32_t i=0;i<samplesPerAxis;++i){
            const int64_t x=cellX*cm+
                (int64_t(2*i+1)*cm)/int64_t(2*samplesPerAxis);
            const int64_t z=cellZ*cm+
                (int64_t(2*j+1)*cm)/int64_t(2*samplesPerAxis);
            if(x<=-edge || x>=edge || z<=-edge || z>=edge)
                continue;
            const Vec3 point{
                Fixed::fromRatio(x,100),
                Fixed::fromRatio(terrain.sampleHeightCm(x,z),100),
                Fixed::fromRatio(z,100)};
            if(!lib::nereid::coversWaterBody(*body,shape,point))
                continue;
            // Water footprint is NOT enough after erosion/landslides.
            // NEREID owns wetness, base surface and flood: only actual
            // submerged samples of the expected body admit a transfer.
            const auto wet=water.sample(point);
            if(wet.found && wet.submerged && wet.body==waterBody)
                return true;
        }
    return false;
}

inline std::vector<NereidNutrientCandidate>
nereidNutrientCandidates(
        const World& world,const EarthworksLayer& terrain,
        const lib::nereid::WaterQueryIndex& water,
        Entity body,Vec3 anchor,
        NereidNutrientJunctionSettings settings) {
    std::vector<NereidNutrientCandidate> out;
    const int64_t cm=ForestDeadwoodResource::kNutrientCellCm;
    const int64_t wx=anchor.x.raw*100/Fixed::kOne;
    const int64_t wz=anchor.z.raw*100/Fixed::kOne;
    if(wx<=-kWorldLimitCm+cm*4 || wx>=kWorldLimitCm-cm*4 ||
       wz<=-kWorldLimitCm+cm*4 || wz>=kWorldLimitCm-cm*4)
        return out;
    const int64_t x0=detail::floorDiv(wx,cm);
    const int64_t z0=detail::floorDiv(wz,cm);
    for(int64_t dz=-int64_t(settings.searchRadiusCells);
        dz<=int64_t(settings.searchRadiusCells);++dz)
        for(int64_t dx=-int64_t(settings.searchRadiusCells);
            dx<=int64_t(settings.searchRadiusCells);++dx){
            const int64_t x=x0+dx,z=z0+dz;
            if(!nereidBodyTouchesNutrientCell(
                    world,terrain,water,body,x,z,settings.samplesPerAxis))
                continue;
            const int64_t cx=x*cm+cm/2,cz=z*cm+cm/2;
            const int64_t ddx=cx-wx,ddz=cz-wz;
            out.push_back({x,z,uint64_t(ddx*ddx+ddz*ddz)});
        }
    std::sort(out.begin(),out.end(),
        [](const auto& a,const auto& b){
            return std::tie(a.distanceSquared,a.x,a.z)<
                   std::tie(b.distanceSquared,b.x,b.z);
        });
    return out;
}

inline std::optional<ForestNutrientJunctionLink>
nereidNutrientLinkedCells(
        const World& world,const EarthworksLayer& terrain,
        const lib::nereid::WaterQueryIndex& water,
        Entity source,Entity target,
        Vec3 sourceAnchor,Vec3 targetAnchor,
        NereidNutrientJunctionSettings settings) {
    if(source==target || source==kNullEntity || target==kNullEntity ||
       !world.alive(source) || !world.alive(target))
        return std::nullopt;
    const auto* from=world.tryGetConst<lib::nereid::WaterBodyComponent>(source);
    const auto* to=world.tryGetConst<lib::nereid::WaterBodyComponent>(target);
    if(!from || !to || !lib::nereid::validWaterBody(*from) ||
       !lib::nereid::validWaterBody(*to) ||
       !lib::nereid::coversWaterBody(
           *from,world.tryGetConst<lib::nereid::WaterGeometryComponent>(source),
           sourceAnchor) ||
       !lib::nereid::coversWaterBody(
           *to,world.tryGetConst<lib::nereid::WaterGeometryComponent>(target),
           targetAnchor))
        return std::nullopt;
    const auto sources=nereidNutrientCandidates(
        world,terrain,water,source,sourceAnchor,settings);
    const auto targets=nereidNutrientCandidates(
        world,terrain,water,target,targetAnchor,settings);
    // Only distinct cells with an unambiguous NEREID body owner are
    // eligible. Cache sets ONCE for each linked pair instead of repeated
    // (potentially expensive) 4m water queries inside the candidate loop.
    using Cell=std::pair<int64_t,int64_t>;
    std::set<Cell> sourceCells,targetCells;
    for(const auto& cell:sources)sourceCells.emplace(cell.x,cell.z);
    for(const auto& cell:targets)targetCells.emplace(cell.x,cell.z);
    for(const auto& a:sources)
        for(const auto& b:targets)
            if((a.x!=b.x || a.z!=b.z) &&
               !targetCells.contains({a.x,a.z}) &&
               !sourceCells.contains({b.x,b.z})){
                ForestNutrientJunctionLink link;
                link.sourceX=a.x;link.sourceZ=a.z;
                link.targetX=b.x;link.targetZ=b.z;
                link.sourceBodyKey=source.packed()+1ull;
                link.targetBodyKey=target.packed()+1ull;
                return link;
            }
    return std::nullopt;
}

// The only sources of topology are LIVE NEREID ECS confluence/cascade
// components. Every mouth is checked against actual linked water-body
// footprints. All emitted links have a unique sorted (ECS id, slot) key.
inline std::vector<ForestNutrientJunctionLink>
buildNereidNutrientJunctionPlan(
        const World& world,const EarthworksLayer& terrain,
        const lib::nereid::WaterQueryIndex& water,
        NereidNutrientJunctionSettings settings={}) {
    validateNereidNutrientJunctionSettings(settings);
    using Slot=std::pair<uint64_t,uint8_t>;
    std::vector<std::pair<Slot,ForestNutrientJunctionLink>> planned;
    const auto append=[&](Entity source,Entity target,
                          Vec3 from,Vec3 to,Slot id) {
        if(const auto link=nereidNutrientLinkedCells(
                world,terrain,water,source,target,from,to,settings))
            planned.emplace_back(id,*link);
        if(planned.size()>settings.maxLinks)
            throw std::length_error("TerraWeave NEREID nutrient junction budget");
    };
    world.eachExistingConst<lib::nereid::WaterConfluenceComponent>(
        [&](Entity entity,const lib::nereid::WaterConfluenceComponent& junction){
            if(!world.alive(entity) ||
               !lib::nereid::validWaterConfluence(junction))return;
            for(uint8_t slot=0;slot<junction.inletCount;++slot)
                append(junction.inlets[slot],junction.outlet,
                       junction.mouth,junction.mouth,{entity.packed(),slot});
        });
    world.eachExistingConst<lib::nereid::WaterCascadeConnectionsComponent>(
        [&](Entity entity,
            const lib::nereid::WaterCascadeConnectionsComponent& links){
            if(!world.alive(entity) || !links.enabled)return;
            const auto* cascade=world.tryGetConst<
                lib::nereid::WaterCascadeComponent>(entity);
            if(!cascade || !lib::nereid::validWaterCascade(*cascade))
                return;
            append(links.inlet,links.outlet,
                   cascade->points.front().position,
                   cascade->points.back().position,
                   {entity.packed(),uint8_t(255)});
        });
    std::sort(planned.begin(),planned.end(),
        [](const auto& a,const auto& b){return a.first<b.first;});
    for(size_t i=0;i<planned.size();++i){
        if(i && planned[i-1].first==planned[i].first)
            throw std::invalid_argument("TerraWeave duplicate NEREID junction");
        planned[i].second.linkKey=uint64_t(i+1);
    }
    std::vector<ForestNutrientJunctionLink> out;
    out.reserve(planned.size());
    for(const auto& link:planned)out.push_back(link.second);
    return out;
}

} // namespace aiverse::terraweave
