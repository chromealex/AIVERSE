// Stage 63: bridge the existing TerraWeave authoritative heightfield into
// NEREID's optional finite-volume flood grid. Terrain NEVER computes water
// motion. NEREID owns the flooded columns, their volumes and the ECS tick.
// The host must seed a bounded window at a structural-write barrier after
// creating water bodies, and may extend windows only by a separate explicit
// reservoir migration (not by blindly reseeding water).
#pragma once
#include "engine/world/TerraWeaveEcsAuthority.h"
#include <aiverselib/water/NereidWaterSystems.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct NereidFloodGridRegion {
    RectCm area{}; // world centimetres, half-open and bounded
    uint32_t maxCells=16384u;
};

inline lib::nereid::WaterFloodResource makeNereidFloodGrid(
        const World& ecs,const EarthworksLayer& ground,
        NereidFloodGridRegion region) {
    const int64_t spacing=ground.spacingCm();
    const auto& r=region.area;
    if(r.minX>=r.maxX || r.minZ>=r.maxZ ||
       r.minX<-100000000+2*spacing ||
       r.minZ<-100000000+2*spacing ||
       r.maxX>100000000-2*spacing ||
       r.maxZ>100000000-2*spacing ||
       !region.maxCells || region.maxCells>16384u)
        throw std::invalid_argument("TerraWeave flood region/size invalid");
    const auto floorDiv=[](int64_t n,int64_t d){
        int64_t q=n/d;
        if(n%d<0)--q;
        return q;
    };
    const int64_t x0=floorDiv(r.minX+spacing-1,spacing);
    const int64_t z0=floorDiv(r.minZ+spacing-1,spacing);
    const int64_t x1=floorDiv(r.maxX-1,spacing);
    const int64_t z1=floorDiv(r.maxZ-1,spacing);
    lib::nereid::WaterFloodResource out;
    out.gridCm=ground.spacingCm();
    out.originXcm=0;out.originZcm=0;
    out.groundRevision=ground.revision();
    if(x1<x0||z1<z0)return out;
    const int64_t width=x1-x0+1,depth=z1-z0+1;
    if(width>region.maxCells || depth>region.maxCells ||
       width*depth>region.maxCells)
        throw std::length_error("TerraWeave flood window exceeds cell budget");
    out.cells.reserve(size_t(width*depth));
    // Scan the SAME NEREID ECS bodies/currents as any water probe.
    // A dry cell remains in the window to receive true overtopping later.
    for(int64_t x=x0;x<=x1;++x)
        for(int64_t z=z0;z<=z1;++z) {
            const int64_t px=x*spacing+spacing/2;
            const int64_t pz=z*spacing+spacing/2;
            const int32_t height=ground.sampleHeightCm(px,pz);
            const auto water=lib::nereid::sampleWater(ecs,{
                Fixed::fromRatio(px,100),Fixed::fromRatio(height,100),
                Fixed::fromRatio(pz,100)});
            lib::nereid::WaterFloodCell cell;
            cell.x=int32_t(x);
            cell.z=int32_t(z);
            cell.groundCm=height;
            if(water.found && water.surfaceY>Fixed::fromRatio(height,100)) {
                const int64_t surface=int64_t(water.surfaceY.raw)*100/
                    Fixed::kOne;
                const int64_t depthCm=surface-height;
                if(depthCm>100000u)
                    throw std::length_error(
                        "TerraWeave flood water column exceeds local reservoir budget");
                if(depthCm>0){
                    cell.volumeCm=uint32_t(depthCm);
                    cell.waterBody=water.body;
                }
            }
            out.cells.push_back(cell);
        }
    lib::nereid::validateNereidFlood(out);
    return out;
}

// Use only at initial map/window publication. Never call this twice during
// gameplay: that would create/destroy water instead of conserving volume.
inline void initializeNereidFloodGrid(
        World& ecs,const EarthworksLayer& ground,
        NereidFloodGridRegion region){
    const auto* existing=ecs.resourceConst<
        lib::nereid::WaterFloodResource>();
    if(existing && (!existing->cells.empty() ||
                    existing->lastTick!=UINT32_MAX))
        throw std::logic_error(
            "NEREID cannot reseed an active flood volume reservoir");
    auto seed=makeNereidFloodGrid(ecs,ground,region);
    ecs.setResource<lib::nereid::WaterFloodResource>(std::move(seed));
}

// Configured finite inflow/outflow ports make an actual river generate a
// water-level rise behind a blocked channel. This reserves a KNOWN finite
// amount of water outside the visible grid; inflow moves it into the bank,
// and outflow returns it to the SAME counted reserve. Host supplies genuine
// NEREID water-body IDs; no implicit infinite-volume pump is created.
// This setter is intentionally usable only before the first simulation tick.
inline void configureNereidFloodBoundaryPorts(
        World& ecs,
        std::vector<lib::nereid::WaterFloodPort> ports,
        uint64_t externalVolumeCm) {
    const auto* existing=ecs.resourceConst<
        lib::nereid::WaterFloodResource>();
    if(!existing || existing->cells.empty() ||
       existing->lastTick!=UINT32_MAX ||
       existing->revision!=0)
        throw std::logic_error(
            "NEREID flood ports must be configured once before first water tick");
    if(externalVolumeCm>1000000000ull)
        throw std::length_error("NEREID external flood reserve too large");
    for(const auto& port:ports) {
        if(port.kind==lib::nereid::WaterFloodPortKind::Inflow){
            const auto* body=ecs.tryGetConst<
                lib::nereid::WaterBodyComponent>(port.waterBody);
            if(!ecs.alive(port.waterBody) || !body ||
               !lib::nereid::validWaterBody(*body))
                throw std::invalid_argument(
                    "TerraWeave flood inlet has no real NEREID water body");
        }
        const auto id=std::pair{port.x,port.z};
        const auto it=std::lower_bound(
            existing->cells.begin(),existing->cells.end(),id,
            [](const lib::nereid::WaterFloodCell& a,
               const std::pair<int32_t,int32_t>& key){
                return std::pair{a.x,a.z}<key;
            });
        if(it==existing->cells.end() ||
           std::pair{it->x,it->z}!=id)
            throw std::invalid_argument(
                "TerraWeave flood port outside existing finite grid");
    }
    auto next=*existing;
    next.ports=std::move(ports);
    next.externalVolumeCm=externalVolumeCm;
    lib::nereid::validateNereidFlood(next);
    ecs.setResource<lib::nereid::WaterFloodResource>(
        std::move(next));
}

// Excavating/erosion/filling displaces the *ground* under the same fluid
// column, preserving saved water volume; the surface rises over fill or
// lowers after excavation without deleting/addition of water. The native
// game applies this at an ECS owner barrier before the next flood tick.
inline bool synchronizeNereidFloodGround(
        World& ecs,const EarthworksLayer& ground) {
    const auto* saved=ecs.resourceConst<
        lib::nereid::WaterFloodResource>();
    if(!saved)return false;
    if(saved->gridCm!=ground.spacingCm())
        throw std::invalid_argument("NEREID flood/TerraWeave grid spacing mismatch");
    if(saved->groundRevision==ground.revision())return false;
    auto next=*saved;
    const int64_t spacing=next.gridCm;
    bool changed=false;
    for(auto& c:next.cells){
        const int64_t px=next.originXcm+int64_t(c.x)*spacing+spacing/2;
        const int64_t pz=next.originZcm+int64_t(c.z)*spacing+spacing/2;
        const int32_t height=ground.sampleHeightCm(px,pz);
        changed |= height!=c.groundCm;
        c.groundCm=height;
    }
    next.groundRevision=ground.revision();
    if(changed){
        if(next.revision==UINT64_MAX)
            throw std::overflow_error("NEREID flood ground revision overflow");
        ++next.revision;
    }
    lib::nereid::validateNereidFlood(next);
    ecs.setResource<lib::nereid::WaterFloodResource>(
        std::move(next));
    return changed;
}

// Opt-in native ECS system AFTER TerraWeave's bank/slump commit, so the NEXT
// NEREID water-routing tick starts from the updated terrain. This does not
// write the canonical soil, only refreshes the flood grid bottom heights.
inline void addTerraWeaveFloodGroundSyncSystem(
        Simulation& sim,std::string name,
        TerraWeaveEcsAuthorityConfig cfg) {
    validateEcsAuthorityConfig(cfg);
    sim.addSystem(std::move(name),
        [cfg=std::move(cfg)](Simulation& current){
            const auto* authority=current.world().resourceConst<
                TerraWeaveAuthorityResource>();
            const auto* flood=current.world().resourceConst<
                lib::nereid::WaterFloodResource>();
            if(!authority || !flood ||
               flood->groundRevision==authority->earth.revision)
                return;
            const auto ground=materializeEcsTerrain(*authority,cfg);
            (void)synchronizeNereidFloodGround(
                current.world(),ground);
        });
}

} // namespace aiverse::terraweave
