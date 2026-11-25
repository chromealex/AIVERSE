// Spherical suspended sediment transport, using NEREID's existing rate
// kernel and canonical Surface adjacency. No terrain or water solver.
#pragma once
#include "NereidPlanetBankErosion.h"
#include "engine/world/nereid/NereidSedimentTransport.h"
#include <map>
#include <tuple>

namespace aiverse::lib::nereid {
struct PlanetBankSedimentTransportComponent {
    uint32_t carryMicroCm3=0;
    uint32_t lastTransportTick=UINT32_MAX;
};
inline void io(Archive& a,PlanetBankSedimentTransportComponent& c){
    a(c.carryMicroCm3,c.lastTransportTick);
    if(c.carryMicroCm3>=1000000u)a.fail();
}
inline void registerPlanetSedimentTransportPersistence(Simulation& sim){
    sim.registerPersistentComponent<PlanetBankSedimentTransportComponent>(
        "nereid.planet_bank_transport",1);
}
struct PlanetSedimentTransfer {
    Entity source=kNullEntity,target=kNullEntity;
    uint64_t volumeCm3=0;
};
struct PlanetSedimentReceipt {
    uint64_t beforeCm3=0,afterCm3=0,movedCm3=0;
    std::vector<PlanetSedimentTransfer> transfers;
};
struct PlanetSedimentSettings {
    uint64_t maxTransferCm3PerTick=1000000000ull;
};
inline constexpr size_t kMaxPlanetSedimentBanks=4096;

// All opted-in banks must have completed their erosion phase for this tick.
// Transport is simultaneous: newly arrived sediment cannot travel again
// until the next tick. Unregistered neighbours, dry banks and other water
// bodies are impermeable boundaries, including across planetary seams.
inline std::optional<PlanetSedimentReceipt> transportPlanetSediment(
        World& world,const game::Surface& surface,uint32_t tick,uint32_t dtMs,
        Fixed seconds,PlanetSedimentSettings settings={}){
    if(!surface.tab||tick==UINT32_MAX||!dtMs||dtMs>1000||
       settings.maxTransferCm3PerTick>kPlanetSedimentCapacityCm3||
       world.countExisting<PlanetBankSedimentTransportComponent>()>kMaxPlanetSedimentBanks)return {};
    struct Bank {
        Entity entity;
        int body;
        PlanetBankSoilComponent soil;
        PlanetBankSedimentTransportComponent transport;
        game::PlanetShoreSample shore;
        uint64_t incoming=0,outgoing=0;
    };
    std::vector<Bank> banks;
    bool valid=true;
    world.eachExistingConst<PlanetBankSedimentTransportComponent>(
        [&](Entity e,const PlanetBankSedimentTransportComponent& transport){
            if(!world.alive(e))return;
            const auto* bank=world.tryGetConst<PlanetBankSoilComponent>(e);
            if(!bank||!validPlanetBank(*bank)||world.has<WaterBankSoilComponent>(e)||
               bank->lastTick!=tick||transport.carryMicroCm3>=1000000u||
               (transport.lastTransportTick!=UINT32_MAX&&transport.lastTransportTick>=tick)){
                valid=false;return;
            }
            const int body=surface.findBody(bank->bodyId);
            int b=-1,f=-1,x=-1,y=-1;
            if(body<0||bank->surfaceCell<0||int64_t(bank->surfaceCell)>=int64_t(surface.W)*surface.H||
               !surface.cellFace(bank->surfaceCell,b,f,x,y)||b!=body){valid=false;return;}
            const auto* sea=world.tryGetConst<WaterSphereComponent>(bank->waterBody);
            if(!sea||!world.alive(bank->waterBody)||sea->center!=game::Surface::anchor(body)){
                valid=false;return;
            }
            const auto shore=game::samplePlanetShore(world,surface,surface.cellCenter(bank->surfaceCell),seconds);
            if(!shore){valid=false;return;}
            banks.push_back({e,body,*bank,transport,*shore});
        });
    if(!valid)return {};
    std::sort(banks.begin(),banks.end(),[](const Bank& a,const Bank& b){
        return std::tuple{a.body,a.soil.surfaceCell,a.entity.packed()}<
               std::tuple{b.body,b.soil.surfaceCell,b.entity.packed()};
    });
    std::map<std::pair<int,int>,size_t> index;
    PlanetSedimentReceipt receipt;
    for(size_t i=0;i<banks.size();++i){
        const auto& b=banks[i];
        if(!index.emplace(std::pair{b.body,b.soil.surfaceCell},i).second)return {};
        receipt.beforeCm3+=b.soil.suspendedCm3;
    }
    const auto wet=[](const Bank& b){
        return b.shore.water.submerged&&b.shore.water.body==b.soil.waterBody;
    };
    for(auto& source:banks){
        source.transport.lastTransportTick=tick;
        if(!wet(source)||!source.soil.suspendedCm3||!settings.maxTransferCm3PerTick)continue;
        const auto radial=surface.cellDir(source.soil.surfaceCell);
        const auto flow=source.shore.water.flow;
        // Select the downstream cardinal edge geometrically. At a face
        // seam, the target's local axes can rotate, so never reuse its X/Z.
        int targetCell=-1;Fixed bestSpeed{};
        const auto radius=surface.bodies[size_t(source.body)].radius;
        Fixed distance{};
        for(int k=0;k<4;++k){
            const int cell=surface.neighbor(source.soil.surfaceCell,k);
            if(cell<0)continue;
            const auto next=surface.cellDir(cell);
            const auto chord=next-radial;
            const auto tangent=chord-radial*chord.dot(radial);
            if(tangent.lengthSq().raw<=0)continue;
            const auto speed=flow.dot(fxNormalize(tangent));
            if(speed>bestSpeed||(speed==bestSpeed&&speed>Fixed{}&&cell<targetCell)){
                bestSpeed=speed;targetCell=cell;distance=chord.length()*radius;
            }
        }
        if(targetCell<0||distance<=Fixed{})continue;
        const auto found=index.find({source.body,targetCell});
        if(found==index.end())continue;
        auto& target=banks[found->second];
        if(!wet(target)||target.soil.waterBody!=source.soil.waterBody)continue;
        const auto distanceCm=uint32_t(std::max<int64_t>(1,(distance.raw*100+Fixed::kOne/2)/Fixed::kOne));
        const auto fraction=sediment_detail::transferFractionMicro(bestSpeed,distanceCm,dtMs);
        const auto desired=std::min(settings.maxTransferCm3PerTick,
            sediment_detail::fractionalTransfer(source.soil.suspendedCm3,fraction,source.transport.carryMicroCm3));
        const auto room=kPlanetSedimentCapacityCm3-target.soil.suspendedCm3-target.incoming;
        const auto moved=std::min({desired,source.soil.suspendedCm3,room});
        if(!moved)continue;
        source.outgoing=moved;target.incoming+=moved;receipt.movedCm3+=moved;
        receipt.transfers.push_back({source.entity,target.entity,moved});
    }
    for(auto& bank:banks){
        bank.soil.suspendedCm3=bank.soil.suspendedCm3-bank.outgoing+bank.incoming;
        receipt.afterCm3+=bank.soil.suspendedCm3;
    }
    if(receipt.beforeCm3!=receipt.afterCm3)return {};
    // No partial updates if the planning phase failed. All writes are
    // made to a private World so COW rollback retains the original mass.
    World staged=world;
    for(const auto& bank:banks){
        staged.get<PlanetBankSoilComponent>(bank.entity)=bank.soil;
        staged.get<PlanetBankSedimentTransportComponent>(bank.entity)=bank.transport;
    }
    world=std::move(staged);return receipt;
}
inline std::optional<PlanetSedimentReceipt> transportGameCorePlanetSediment(
        World& world,uint64_t expectedTerrainRevision,uint32_t tick,uint32_t dtMs,
        Fixed seconds,PlanetSedimentSettings settings={}){
    const auto* gs=world.resourceConst<game::GameState>();
    if(!gs||!gs->surface||gs->planetTerrainRevision!=expectedTerrainRevision)return {};
    const auto surface=gs->surface;
    return transportPlanetSediment(world,*surface,tick,dtMs,seconds,settings);
}
} // namespace aiverse::lib::nereid
