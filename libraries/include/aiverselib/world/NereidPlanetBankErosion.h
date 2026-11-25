// Opt-in spherical NEREID bank authority: actual suspended volume in cm^3.
// Never attach the planar cm-equivalent bank component to the same entity.
// The existing NEREID material policy proposes height demand; only the
// canonical GameCore Surface edit determines the committed sediment volume.
#pragma once
#include "engine/game/GameCore.h"
#include "engine/game/PlanetTerrainWater.h"
#include "engine/world/nereid/NereidShorelineSystems.h"
#include "GranularPlanetSoilGeometry.h"
#include "FracturePlanetFoundation.h"
#include "EvolvePlanetRootSupport.h"

namespace aiverse::lib::nereid {
struct PlanetBankSoilComponent {
    std::string bodyId;
    int32_t surfaceCell=-1;
    Entity waterBody=kNullEntity;
    ShoreSoil soil=ShoreSoil::Loam;
    ShoreErosionSettings settings{};
    int16_t bedrock16=-320,ceiling16=320;
    uint32_t rootBindingPermille=0;
    uint64_t suspendedCm3=0;
    uint32_t erosionCarryMilliCm=0,depositionCarryMilliCm=0;
    int32_t demand100=0; // hundredths of 1/16 metre quantum, positive = fill
    uint32_t lastTick=UINT32_MAX;
};
inline constexpr uint64_t kPlanetSedimentCapacityCm3=1000000000000000ull;
inline bool validPlanetBank(const PlanetBankSoilComponent& b){
    return !b.bodyId.empty()&&b.bodyId.size()<=128&&b.surfaceCell>=0&&b.waterBody.valid()&&
        uint32_t(b.soil)<=uint32_t(ShoreSoil::Rock)&&b.bedrock16<=b.ceiling16&&
        b.rootBindingPermille<=1000&&b.suspendedCm3<=kPlanetSedimentCapacityCm3&&
        b.erosionCarryMilliCm<1000&&b.depositionCarryMilliCm<1000&&
        b.demand100>=-3200&&b.demand100<=3200&&b.settings.maxCutCmPerStep<=100&&
        b.settings.maxDepositCmPerStep<=100&&b.settings.ratePermille<=10000;
}
inline void io(Archive& a,PlanetBankSoilComponent& b){
    a(b.bodyId,b.surfaceCell,b.waterBody,b.soil,b.settings,b.bedrock16,b.ceiling16,
      b.rootBindingPermille,b.suspendedCm3,b.erosionCarryMilliCm,b.depositionCarryMilliCm,
      b.demand100,b.lastTick);
    if(!validPlanetBank(b))a.fail();
}
inline void registerPlanetBankErosionPersistence(Simulation& sim){
    sim.registerPersistentComponent<PlanetBankSoilComponent>("nereid.planet_bank_soil",1);
}
struct PlanetBankErosionPlan {
    PlanetBankSoilComponent bank;
    std::optional<granular::PlanetSoilGeometryPreview> geometry;
};
// A virtual 1cm-equivalent column is used ONLY to evaluate NEREID's
// rate policy. Its output sediment value is discarded, never persisted.
// Actual erosion/deposition is admitted by the exact discrete cm^3 receipt.
inline std::optional<PlanetBankErosionPlan> planPlanetBankErosion(
        const World& world,const game::Surface& surface,const PlanetBankSoilComponent& bank,
        uint64_t revision,uint32_t tick,uint32_t dtMs,Fixed seconds,
        uint32_t wave=0,uint32_t slope=0,uint32_t shield=0){
    if(!validPlanetBank(bank)||tick==UINT32_MAX||!dtMs||dtMs>1000||
       (bank.lastTick!=UINT32_MAX&&tick<=bank.lastTick))return {};
    const int body=surface.findBody(bank.bodyId);
    const auto footprint=worldprocess::planetTerrainFootprint(surface,body,bank.surfaceCell);
    if(!footprint)return {};
    const auto* sea=world.tryGetConst<WaterSphereComponent>(bank.waterBody);
    if(!sea||!world.alive(bank.waterBody)||sea->center!=game::Surface::anchor(body))return {};
    const auto shore=game::samplePlanetShore(world,surface,surface.cellCenter(bank.surfaceCell),
        seconds,wave,slope,shield);
    if(!shore)return {};
    PlanetBankErosionPlan result{bank,{}};result.bank.lastTick=tick;
    if(!shore->forcing.wetContactPermille||shore->water.body!=bank.waterBody||bank.soil==ShoreSoil::Rock)return result;
    const auto cm=[](int16_t h){return int32_t(h)*100/16;};
    int16_t minCorner=INT16_MAX,maxCorner=INT16_MIN;
    for(const auto& c:footprint->corners){
        minCorner=std::min(minCorner,c.beforeHeight16);maxCorner=std::max(maxCorner,c.beforeHeight16);
    }
    if(minCorner<bank.bedrock16||maxCorner>bank.ceiling16)return {};
    ShoreErosionCell virtualSoil;
    virtualSoil.soil=bank.soil;virtualSoil.groundCm=cm(minCorner);
    virtualSoil.bedrockCm=cm(bank.bedrock16);
    virtualSoil.suspendedCm=bank.suspendedCm3?1000u:0u;
    virtualSoil.erosionCarryMilliCm=bank.erosionCarryMilliCm;
    virtualSoil.depositionCarryMilliCm=bank.depositionCarryMilliCm;
    virtualSoil.rootBindingPermille=bank.rootBindingPermille;
    const auto demand=erodeShore(virtualSoil,shore->forcing,dtMs,bank.settings);
    result.bank.erosionCarryMilliCm=demand.cell.erosionCarryMilliCm;
    result.bank.depositionCarryMilliCm=demand.cell.depositionCarryMilliCm;
    result.bank.demand100=std::clamp(bank.demand100+
        (int32_t(demand.depositedCm)-int32_t(demand.removedCm))*16,-3200,3200);
    const bool fill=result.bank.demand100>0;
    const int availableQuanta=fill?int(bank.ceiling16)-maxCorner:int(minCorner)-bank.bedrock16;
    const int quanta=std::min({std::abs(result.bank.demand100)/100,availableQuanta,32});
    if(!quanta)return result;
    granular::PlanetSoilTarget target;
    target.body=body;target.bodyId=bank.bodyId;target.surfaceCell=bank.surfaceCell;
    target.footprint=*footprint;target.expectedTerrainRevision=revision;target.completedTick=tick;
    target.direction=fill?granular::PlanetSoilDirection::Fill:granular::PlanetSoilDirection::Dig;
    target.volumeBudgetCm3=fill?bank.suspendedCm3:kPlanetSedimentCapacityCm3-bank.suspendedCm3;
    // Pick a whole affordable height quantum, never create free soil.
    for(int q=quanta;q>0;--q){
        auto edit=granular::previewPlanetSoilGeometry(surface,target,q,bank.bedrock16,bank.ceiling16);
        if(!edit)continue;
        const auto amount=edit->movedVolumeCm3;
        result.bank.suspendedCm3=fill?bank.suspendedCm3-amount:bank.suspendedCm3+amount;
        result.bank.demand100-=edit->heightDelta16*100;
        result.geometry=std::move(edit);break;
    }
    return result;
}
struct PlanetBankErosionReceipt {
    uint64_t beforeRevision=0,afterRevision=0,beforeSedimentCm3=0,afterSedimentCm3=0,movedVolumeCm3=0;
    int32_t heightDelta16=0;
    evolve::PlanetRootSupportOutcome roots;
};
// One bank command at the serialized completed-tick barrier. Multiple
// commands must use stable ordering and the latest terrain revision.
// Owner records input and durable plant/debris consequences separately.
inline std::optional<PlanetBankErosionReceipt> commitPlanetBankErosion(
        game::GameCore& core,World& world,Entity entity,uint64_t expectedRevision,
        uint32_t tick,uint32_t dtMs,Fixed seconds,uint32_t wave=0,uint32_t slope=0,uint32_t shield=0,
        std::span<const fracture::PlanetFoundation> foundations={},
        std::span<const evolve::PlanetRootBinding> roots={}){
    const auto* gs=world.resourceConst<game::GameState>();
    const auto* bank=world.tryGetConst<PlanetBankSoilComponent>(entity);
    if(!world.alive(entity)||!bank||world.has<WaterBankSoilComponent>(entity)||
       !gs||!gs->surface||!gs->grid.hf||gs->planetTerrainRevision!=expectedRevision)return {};
    const auto source=gs->surface;
    const auto plan=planPlanetBankErosion(world,*source,*bank,expectedRevision,tick,dtMs,seconds,wave,slope,shield);
    if(!plan)return {};
    PlanetBankErosionReceipt out;
    out.beforeRevision=out.afterRevision=expectedRevision;
    out.beforeSedimentCm3=bank->suspendedCm3;out.afterSedimentCm3=plan->bank.suspendedCm3;
    World staged=world;
    if(plan->geometry){
        if(!core.stagePlanetTerrain(staged,*source,expectedRevision,*plan->geometry->surface))return {};
        const auto next=staged.resourceConst<game::GameState>()->surface;
        const auto support=fracture::reconcilePlanetFoundations(staged,*source,*next,foundations,tick);
        if(!support||!core.reconcilePlanetGroundUnits(staged,*source,support->preservePose))return {};
        if(!roots.empty()){
            auto changed=evolve::reconcilePlanetRoots(staged,*next,roots,expectedRevision+1,tick);
            if(!changed)return {};
            out.roots=std::move(*changed);
        }
        out.afterRevision=expectedRevision+1;
        out.movedVolumeCm3=plan->geometry->movedVolumeCm3;
        out.heightDelta16=plan->geometry->heightDelta16;
    }
    staged.get<PlanetBankSoilComponent>(entity)=plan->bank;
    world=std::move(staged);return out;
}
} // namespace aiverse::lib::nereid
