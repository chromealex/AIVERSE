// Opt-in deterministic planetary bank pipeline for GameCore native projects.
// Runs after the existing hydrated ECS systems. A complete cadence batch is
// staged on one World; no Box3D/GPU or external side effects in simulation.
#pragma once
#include "NereidPlanetSedimentTransport.h"
#include <map>
#include <set>
#include <stdexcept>

namespace aiverse::lib::worldprocess {
struct PlanetBankPipelineState {
    uint32_t lastAttempt=UINT32_MAX,lastSuccess=UINT32_MAX;
    uint64_t rejectedBatches=0,successfulBatches=0;
    uint64_t lastMovedSoilCm3=0,lastTransportCm3=0;
};
inline void io(Archive& a,PlanetBankPipelineState& s){
    a(s.lastAttempt,s.lastSuccess,s.rejectedBatches,s.successfulBatches,
      s.lastMovedSoilCm3,s.lastTransportCm3);
    if(!a.writing()&&s.lastSuccess!=UINT32_MAX&&
       (s.lastAttempt==UINT32_MAX||s.lastSuccess>s.lastAttempt))a.fail();
}
struct PlanetBankPipelineConfig {
    uint32_t intervalTicks=1;
    uint32_t dtMs=16;
    nereid::PlanetSedimentSettings transport{};
    struct Bank {Entity entity;uint32_t wave=0,slope=0,shield=0;};
    std::vector<Bank> banks;
    std::vector<fracture::PlanetFoundation> foundations;
    bool rootFalls=false;
};
inline constexpr size_t kMaxScheduledPlanetBanks=16;
inline bool runPlanetBankPipeline(game::GameCore& core,World& world,
        const PlanetBankPipelineConfig& config,uint32_t tick,Fixed seconds){
    using namespace nereid;
    const auto* status=world.resourceConst<PlanetBankPipelineState>();
    if(!status||tick==UINT32_MAX||!config.intervalTicks||tick%config.intervalTicks||
       (status->lastAttempt!=UINT32_MAX&&status->lastAttempt>=tick))return false;
    auto report=*status;report.lastAttempt=tick;
    const auto reject=[&](){
        if(report.rejectedBatches!=UINT64_MAX)++report.rejectedBatches;
        world.setResource<PlanetBankPipelineState>(report);return false;
    };
    if(config.banks.empty()||config.banks.size()>kMaxScheduledPlanetBanks||
       !config.dtMs||config.dtMs>1000||report.successfulBatches==UINT64_MAX)return reject();
    const auto* gs=world.resourceConst<game::GameState>();
    if(!gs||!gs->surface||!gs->grid.hf)return reject();
    const auto surface=gs->surface;
    std::vector<evolve::PlanetRootBinding> rootBindings;
    if(const auto* roots=world.resourceConst<terraweave::TerraWeaveAuthorityResource>();
       roots&&!roots->rooted.empty()){
        const auto* delivery=world.resourceConst<evolve::PlanetRootDeliveryResource>();
        if(!config.rootFalls||!delivery||delivery->bindings.size()!=roots->rooted.size())return reject();
        rootBindings=delivery->bindings;
        std::set<terraweave::AnchoredPlantId> ids;
        for(const auto& root:roots->rooted)ids.insert({root.anchor.speciesId,root.anchor.plantKey});
        for(const auto& binding:rootBindings)if(!ids.erase(binding.id))return reject();
        if(!ids.empty())return reject();
    }
    std::set<std::pair<int,int>> cells;
    std::set<uint64_t> entities,foundations;
    std::set<int> affectedBodies;
    for(const auto& f:config.foundations){
        if(!world.alive(f.member)||!world.has<fracture::Member>(f.member)||
           !foundations.insert(f.member.packed()).second)return reject();
    }
    auto banks=config.banks;
    for(const auto& entry:banks){
        const auto* bank=world.tryGetConst<PlanetBankSoilComponent>(entry.entity);
        if(!bank||!world.alive(entry.entity)||!validPlanetBank(*bank)||
           !world.has<PlanetBankSedimentTransportComponent>(entry.entity)||
           world.has<WaterBankSoilComponent>(entry.entity)||
           !entities.insert(entry.entity.packed()).second||
           entry.wave>1000||entry.slope>8000||entry.shield>1000)return reject();
        const int body=surface->findBody(bank->bodyId);
        if(!planetTerrainFootprint(*surface,body,bank->surfaceCell)||
           !cells.emplace(body,bank->surfaceCell).second)return reject();
        affectedBodies.insert(body);
    }
    if(world.countExisting<PlanetBankSedimentTransportComponent>()!=banks.size())return reject();
    bool complete=true;
    world.eachExistingConst<fracture::Member>([&](Entity e,const fracture::Member& member){
        if(!world.alive(e)||!member.anchored||member.broken||member.detached)return;
        const auto* tr=world.tryGetConst<Transform>(e);
        if(!tr){complete=false;return;}
        const auto frame=surfaceFrame(surface.get(),tr->pos);
        if(frame&&affectedBodies.contains(frame->body)&&!foundations.contains(e.packed()))complete=false;
    });
    if(!complete)return reject();
    std::sort(banks.begin(),banks.end(),[&](const auto& a,const auto& b){
        const auto& x=*world.tryGetConst<PlanetBankSoilComponent>(a.entity);
        const auto& y=*world.tryGetConst<PlanetBankSoilComponent>(b.entity);
        return std::tuple{x.bodyId,x.surfaceCell,a.entity.packed()}<
               std::tuple{y.bodyId,y.surfaceCell,b.entity.packed()};
    });
    World staged=world;
    uint64_t moved=0;
    for(const auto& entry:banks){
        const auto revision=staged.resourceConst<game::GameState>()->planetTerrainRevision;
        const auto receipt=commitPlanetBankErosion(core,staged,entry.entity,revision,tick,
            config.dtMs,seconds,entry.wave,entry.slope,entry.shield,config.foundations,rootBindings);
        if(!receipt||receipt->movedVolumeCm3>UINT64_MAX-moved)return reject();
        moved+=receipt->movedVolumeCm3;
    }
    const auto revision=staged.resourceConst<game::GameState>()->planetTerrainRevision;
    const auto transport=transportGameCorePlanetSediment(staged,revision,tick,config.dtMs,seconds,config.transport);
    if(!transport)return reject();
    report.lastSuccess=tick;++report.successfulBatches;
    report.lastMovedSoilCm3=moved;report.lastTransportCm3=transport->movedCm3;
    staged.setResource<PlanetBankPipelineState>(report);
    world=std::move(staged);return true;
}
inline int64_t planetBankInteger(const Json& j,const std::string& key,int64_t fallback,int64_t min,int64_t max){
    if(!j.has(key))return fallback;
    const auto& v=j[key];const auto n=v.integer();
    if(!v.isNumber()||n<min||n>max||v.fx()!=Fixed::fromInt(n))
        throw std::invalid_argument("planetBanks invalid integer: "+key);
    return n;
}
inline void installPlanetBankPipeline(game::GameCore& core,Simulation& sim,const Json& json){
    using namespace nereid;
    const auto* gs=sim.world().resourceConst<game::GameState>();
    if(!json.isObject()||!gs||!gs->surface||!gs->grid.hf||
       !json["banks"].isArray()||!json["banks"].size()||json["banks"].size()>kMaxScheduledPlanetBanks)
        throw std::invalid_argument("planetBanks requires a heightfield planet and 1..16 banks");
    for(const auto& [key,value]:json.members())
        if(key!="intervalTicks"&&key!="maxTransferCm3"&&key!="banks"&&key!="foundations"&&key!="rootFalls")
            throw std::invalid_argument("planetBanks unknown key: "+key);
    const auto surface=gs->surface;
    PlanetBankPipelineConfig cfg;
    if(json.has("rootFalls")&&json["rootFalls"].type()!=Json::Type::Bool)
        throw std::invalid_argument("planetBanks rootFalls must be Boolean");
    cfg.rootFalls=json.getBool("rootFalls");
    cfg.intervalTicks=uint32_t(planetBankInteger(json,"intervalTicks",1,1,240));
    const uint64_t duration=uint64_t(cfg.intervalTicks)*1000;
    if(!sim.tickRate()||duration>uint64_t(sim.tickRate())*1000)
        throw std::invalid_argument("planetBanks interval must be <=1 simulated second");
    cfg.dtMs=uint32_t((duration+sim.tickRate()/2)/sim.tickRate());
    if(!cfg.dtMs)throw std::invalid_argument("planetBanks interval below millisecond resolution");
    cfg.transport.maxTransferCm3PerTick=uint64_t(planetBankInteger(json,"maxTransferCm3",1000000000,0,1000000000));
    auto& w=sim.world();
    const auto& authored=core.matchMap()["entities"];
    if(!authored.isArray()||authored.size()!=core.mapPlaced().size())
        throw std::invalid_argument("planetBanks map entity pairing");
    std::map<std::string,Entity> ids;
    for(size_t i=0;i<authored.size();++i){
        const auto id=authored[i].getStr("id");
        if(!id.empty()&&!ids.emplace(id,core.mapPlaced()[i]).second)
            throw std::invalid_argument("planetBanks duplicate map ID");
    }
    const auto resolve=[&](const Json& j)->Entity{
        if(!j.isString()||!ids.contains(j.str())||!w.alive(ids.at(j.str())))
            throw std::invalid_argument("planetBanks unresolved map entity");
        return ids.at(j.str());
    };
    std::set<std::pair<int,int>> cells;
    std::set<uint64_t> bankIds,foundationIds;
    struct Initial {Entity entity;PlanetBankSoilComponent bank;};
    std::vector<Initial> initial;
    for(const auto& item:json["banks"].items()){
        if(!item.isObject())throw std::invalid_argument("planetBanks invalid bank");
        for(const auto& [key,value]:item.members())
            if(key!="entity"&&key!="soil"&&key!="bedrock16"&&key!="ceiling16"&&key!="rootBindingPermille"&&
               key!="waveEnergyPermille"&&key!="bankSlopePermille"&&key!="shieldPermille"&&key!="ratePermille")
                throw std::invalid_argument("planetBanks unknown bank key: "+key);
        const auto entity=resolve(item["entity"]);
        const auto* tr=w.tryGetConst<Transform>(entity);
        if(!tr||!bankIds.insert(entity.packed()).second||w.has<PlanetBankSoilComponent>(entity)||
           w.has<PlanetBankSedimentTransportComponent>(entity)||w.has<WaterBankSoilComponent>(entity))
            throw std::invalid_argument("planetBanks duplicate or missing bank owner");
        const auto frame=surfaceFrame(surface.get(),tr->pos);
        if(!frame||!cells.emplace(frame->body,frame->cell).second)
            throw std::invalid_argument("planetBanks duplicate or invalid cell");
        PlanetBankSoilComponent bank;
        bank.bodyId=surface->bodies[size_t(frame->body)].id;bank.surfaceCell=frame->cell;
        size_t seas=0;
        w.eachExistingConst<WaterSphereComponent>([&](Entity e,const WaterSphereComponent& sea){
            if(w.alive(e)&&validWaterSphere(sea)&&sea.center==game::Surface::anchor(frame->body)){
                bank.waterBody=e;++seas;
            }
        });
        if(seas!=1)throw std::invalid_argument("planetBanks needs exactly one sea per bank body");
        bank.soil=ShoreSoil(planetBankInteger(item,"soil",2,0,5));
        bank.bedrock16=int16_t(planetBankInteger(item,"bedrock16",-320,INT16_MIN,INT16_MAX));
        bank.ceiling16=int16_t(planetBankInteger(item,"ceiling16",320,INT16_MIN,INT16_MAX));
        bank.rootBindingPermille=uint32_t(planetBankInteger(item,"rootBindingPermille",0,0,1000));
        bank.settings.ratePermille=uint32_t(planetBankInteger(item,"ratePermille",1000,0,10000));
        if(!validPlanetBank(bank))throw std::invalid_argument("planetBanks invalid bank limits");
        const auto footprint=planetTerrainFootprint(*surface,frame->body,frame->cell);
        if(!footprint)throw std::invalid_argument("planetBanks invalid terrain footprint");
        for(const auto& c:footprint->corners)
            if(c.beforeHeight16<bank.bedrock16||c.beforeHeight16>bank.ceiling16)
                throw std::invalid_argument("planetBanks authored height outside bank bounds");
        cfg.banks.push_back({entity,
            uint32_t(planetBankInteger(item,"waveEnergyPermille",0,0,1000)),
            uint32_t(planetBankInteger(item,"bankSlopePermille",0,0,8000)),
            uint32_t(planetBankInteger(item,"shieldPermille",0,0,1000))});
        initial.push_back({entity,std::move(bank)});
    }
    if(json.has("foundations")&&!json["foundations"].isArray())
        throw std::invalid_argument("planetBanks foundations must be array");
    if(json["foundations"].size()>fracture::kMaxMembers)
        throw std::invalid_argument("planetBanks foundation budget");
    for(const auto& item:json["foundations"].items()){
        const auto entity=resolve(item);const auto* tr=w.tryGetConst<Transform>(entity);
        const auto* member=w.tryGetConst<fracture::Member>(entity);
        if(!tr||!member||!member->anchored||!foundationIds.insert(entity.packed()).second)
            throw std::invalid_argument("planetBanks invalid fixed foundation");
        const auto frame=surfaceFrame(surface.get(),tr->pos);
        if(!frame)throw std::invalid_argument("planetBanks invalid foundation placement");
        cfg.foundations.push_back({entity,surface->bodies[size_t(frame->body)].id,{tr->pos},1,Fixed::fromRatio(1,8)});
    }
    registerPlanetBankErosionPersistence(sim);registerPlanetSedimentTransportPersistence(sim);
    if(cfg.rootFalls)evolve::registerPlanetRootDeliveryPersistence(sim);
    sim.registerPersistentResource<PlanetBankPipelineState>("worldprocess.planet_bank_pipeline",1);
    w.setResource<PlanetBankPipelineState>();
    for(const auto& item:initial){
        w.add<PlanetBankSoilComponent>(item.entity,item.bank);
        w.add<PlanetBankSedimentTransportComponent>(item.entity,PlanetBankSedimentTransportComponent{});
    }
    sim.addSystem("worldprocess.planet_bank_pipeline",[cfg=std::move(cfg)](Simulation& current){
        if(current.tick()%cfg.intervalTicks)return;
        auto* host=dynamic_cast<game::GameCore*>(&current.module());
        if(host)(void)runPlanetBankPipeline(*host,current.world(),cfg,current.tick(),
            current.dt()*Fixed::fromInt(current.tick()));
    });
}
} // namespace aiverse::lib::worldprocess
