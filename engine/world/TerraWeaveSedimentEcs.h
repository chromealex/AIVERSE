// TerraWeave/NEREID Stage 58: ECS adapter for NEREID's conservative
// suspended-sediment transport. Register AFTER NEREID bank-erosion and
// BEFORE TerraWeave's ground height commit. Transport never recomputes
// erosion, moves terrain or mutates water geometry; only suspended sediment
// and per-bank fractional carry are updated in persistent ECS components.
#pragma once
#include "engine/world/TerraWeaveEcsBankBridge.h"
#include "engine/world/nereid/NereidSedimentTransport.h"
#include "engine/world/nereid/NereidConfluences.h"
#include "engine/world/nereid/NereidCascades.h"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

inline void registerNereidSedimentEcsPersistence(Simulation& sim){
    sim.registerPersistentComponent<
        lib::nereid::WaterBankSedimentTransportComponent>(
        "nereid.bank_sediment_transport",1);
}

struct EcsSedimentTransportResult {
    uint32_t visitedBanks=0;
    uint32_t transferCount=0;
    uint64_t transferredCm=0;
    uint64_t sedimentMassCm=0;
};

// Run once on the owner ECS simulation tick, before any bank-cell structural
// writes. Inputs come only from current-tick NEREID WaterContact snapshots.
// Both soil and transport ECS pools are detached for COW snapshots before
// writes, preserving rollback. The pure transport computation prevalidates
// all bank identities, capacities and mass conservation before publication.
inline EcsSedimentTransportResult transportNereidBankSediment(
        World& ecs,uint32_t tick,int32_t spacingCm,
        uint32_t dtMs,
        lib::nereid::SedimentTransportSettings settings={}) {
    using namespace lib::nereid;
    std::map<std::pair<int64_t,int64_t>,Entity> entityOf;
    std::vector<SedimentTransportCell> samples;
    const World& read=ecs;
    read.eachExistingConst<BankEarthVertexComponent>(
        [&](Entity entity,const BankEarthVertexComponent& binding) {
            const auto* bank=read.tryGetConst<WaterBankSoilComponent>(entity);
            const auto* contact=read.tryGetConst<WaterContactComponent>(entity);
            const auto* transport=read.tryGetConst<
                WaterBankSedimentTransportComponent>(entity);
            if(!bank || !bank->enabled || !contact ||
               contact->sampledTick!=tick || !contact->sample.found ||
               !transport)return;
            if(transport->lastTransportTick==tick)
                throw std::invalid_argument(
                    "NEREID sediment transport already performed on tick");
            if(!entityOf.emplace(
                    std::make_pair(binding.vertex.x,binding.vertex.z),
                    entity).second)
                throw std::invalid_argument(
                    "NEREID two banks own one sediment grid coordinate");
            if(samples.size()>=16384u)
                throw std::length_error("NEREID sediment ECS bank budget");
            samples.push_back({
                binding.vertex.x,binding.vertex.z,
                bank->soil.suspendedCm,transport->carryMicroCm,
                bank->wetContactPermille,contact->sample.flow,
                contact->sample.body});
        });

    // NEREID-authored junctions connect *different* WaterBodyComponent
    // identities; transport is one-way from inlet to outlet, never inferred
    // from two overlapping surfaces. Cascades link their own body pairs.
    std::vector<SedimentWaterLink> links;
    read.eachExistingConst<WaterConfluenceComponent>(
        [&](Entity,const WaterConfluenceComponent& junction){
            if(!validWaterConfluence(junction))return;
            const auto* outlet=read.tryGetConst<WaterBodyComponent>(
                junction.outlet);
            if(!outlet || !validWaterBody(*outlet) ||
               !coversWaterBody(*outlet,
                    read.tryGetConst<WaterGeometryComponent>(
                        junction.outlet),junction.mouth))
                return;
            // Match NEREID confluence policy: all linked water bodies
            // must actually cover their authored shared mouth.
            for(size_t i=0;i<junction.inletCount;++i){
                const Entity inlet=junction.inlets[i];
                const auto* body=read.tryGetConst<WaterBodyComponent>(inlet);
                if(!body || !validWaterBody(*body) ||
                   !coversWaterBody(*body,
                        read.tryGetConst<WaterGeometryComponent>(inlet),
                        junction.mouth))
                    return;
            }
            for(size_t i=0;i<junction.inletCount;++i)
                links.push_back({junction.inlets[i],junction.outlet});
        });
    read.eachExistingConst<WaterCascadeConnectionsComponent>(
        [&](Entity junction,const WaterCascadeConnectionsComponent& link){
            const auto* cascade=read.tryGetConst<WaterCascadeComponent>(
                junction);
            if(!link.enabled || !link.inlet.valid() ||
               !link.outlet.valid() || link.inlet==link.outlet ||
               !cascade || !validWaterCascade(*cascade))
                return;
            const auto* source=read.tryGetConst<WaterBodyComponent>(
                link.inlet);
            const auto* target=read.tryGetConst<WaterBodyComponent>(
                link.outlet);
            if(!source || !target ||
               !validWaterBody(*source) || !validWaterBody(*target))
                return;
            links.push_back({link.inlet,link.outlet});
        });
    // The transport kernel treats links as a set but enforces its 1024
    // budget on the raw count: the same body pair authored by a confluence
    // and a cascade (or several cascades) must not trip that limit and
    // throw inside the system. Canonical (packed id) order, deterministic.
    std::sort(links.begin(),links.end(),
        [](const SedimentWaterLink& a,const SedimentWaterLink& b){
            return a.inlet.packed()==b.inlet.packed() ?
                a.outlet.packed()<b.outlet.packed() :
                a.inlet.packed()<b.inlet.packed();
        });
    links.erase(std::unique(links.begin(),links.end(),
        [](const SedimentWaterLink& a,const SedimentWaterLink& b){
            return a.inlet==b.inlet && a.outlet==b.outlet;
        }),links.end());
    const auto changed=advectSuspendedSediment(
        std::move(samples),spacingCm,dtMs,settings,links);
    // No callbacks or ECS structural writes during the publication loop.
    // Obtain ALL mutable pools before changing either type.
    // pool<T> detaches COW ownership; tryGet detaches the relevant page.
    // Using prepareExistingWrite alone would mutate rollback snapshots.
    auto* soils=&ecs.pool<WaterBankSoilComponent>();
    auto* fractions=&ecs.pool<WaterBankSedimentTransportComponent>();
    for(const auto& cell:changed.cells){
        const auto id=entityOf.at({cell.gridX,cell.gridZ});
        auto* soil=soils?soils->tryGet(id.index):nullptr;
        auto* fraction=fractions?fractions->tryGet(id.index):nullptr;
        if(!soil || !fraction)
            throw std::logic_error(
                "NEREID sediment bank ECS components disappeared");
        soil->soil.suspendedCm=cell.suspendedCm;
        fraction->carryMicroCm=cell.carryMicroCm;
        fraction->lastTransportTick=tick;
    }
    return {
        uint32_t(changed.cells.size()),
        uint32_t(changed.transfers.size()),
        changed.totalMovedCm,changed.totalAfterCm};
}

inline void addNereidSedimentTransportSystem(
        Simulation& sim,std::string name,int32_t spacingCm,
        uint32_t dtMs=16,
        lib::nereid::SedimentTransportSettings settings={}) {
    if(spacingCm<10 || spacingCm>10000 ||
       dtMs==0 || dtMs>1000 ||
       settings.maxTransferCmPerTick>1000u)
        throw std::invalid_argument(
            "NEREID sediment ECS transport settings");
    sim.addSystem(std::move(name),
        [spacingCm,dtMs,settings](Simulation& current){
            (void)transportNereidBankSediment(
                current.world(),current.tick(),spacingCm,dtMs,settings);
        });
}

} // namespace aiverse::terraweave
