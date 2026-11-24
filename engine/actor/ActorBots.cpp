// `fps.bots`: map players with "bot": true run the bots-combatant controller (libraries/.../bots/Fps.h) inside the
// authoritative tick instead of the reference fallback bot. Navigation: a navmesh per generated building
// (doors, stairs, floors) plus straight steering in the open; outside a building a goal inside it is
// replaced by its door. Goals besides fighting: the battle-royale zone (inside the controller), wanted
// loot nearby (weapons not owned, better gear, medkits / ammo), healing when hurt and out of combat.
// Bot state is the BotMemory component (ActorState bytes, saved and hashed); meshes are derived from collision at setup.
#include "ActorModule.h"
#include "engine/bots/Fps.h"
#include "engine/bots/Persistence.h"
#include <algorithm>
namespace aiverse::actor {
struct BotHost { lib::bots::ActorConfig config;std::vector<lib::bots::NavMesh> meshes; };
// Rebuild only derived navigation geometry; never reset BotMemory, controller
// state or gameplay RNG. Failed construction leaves previous meshes intact.
bool ActorHost::rebuildBotNavigation(std::string* error) {
    if(!botHost_)return true;
    std::vector<lib::bots::NavMesh> meshes;
    meshes.reserve(buildingRegions_.size());
    for(const auto& region:buildingRegions_){
        lib::bots::NavBuildConfig c;c.bounds=region.bounds;c.cellSize=Fixed::fromRatio(1,2);
        c.radius=config_.motor.radius;c.height=config_.motor.height;c.stepHeight=config_.motor.stepHeight;
        auto built=lib::bots::buildNavMesh(collision_,c);
        if(!built.mesh) {
            if(error)*error="actor.bots: "+built.error;
            return false;
        }
        meshes.push_back(std::move(*built.mesh));
    }
    botHost_->meshes=std::move(meshes);
    ++navigationRebuilds_;
    return true;
}
void ActorHost::setupBots(Simulation& sim,const Json&) {
    botHost_.reset();
    navigationCollisionRevision_=UINT64_MAX;
    navigationRebuilds_=0;
    if(config_.bots.isNull())return;
    auto host=std::make_shared<BotHost>();host->config=lib::bots::fpsConfig(config_.bots);
    botHost_=host;
    // Streamed collision is not yet available at setup. Defer derived mesh
    // construction until readyToStep has published all required chunks.
    if(!streamManifest_.getBool("async")) {
        std::string error;
        if(!rebuildBotNavigation(&error))throw JsonError(error);
    }
    for(const Entity e:sim.world().resourceConst<CharacterIndex>()->bySlot)
        sim.world().add<BotMemory>(e,BotMemory{lib::bots::saveFps({})});
}
namespace {
bool insideXZ(const world::Bounds& b,Vec3 p){return p.x>=b.min.x&&p.x<=b.max.x&&p.z>=b.min.z&&p.z<=b.max.z&&p.y>=b.min.y&&p.y<=b.max.y;}
}
bool ActorHost::thinkBot(Simulation& sim,CharacterRef a) {
    if(!botHost_)return false;
    auto& s=state(sim);World& w=sim.world();auto now=sim.tick();const auto& host=*botHost_;
    lib::bots::ActorState bot;if(const auto* memory=w.tryGet<BotMemory>(a.entity);!memory||!lib::bots::loadFps(memory->bytes,bot))bot={};
    // Only safe, noninteractive decision work can be decimated. The ordinary
    // tick still runs every motor, collision, health and weapon system. Do
    // not reuse held actions, and do not sleep an actor with a remembered
    // target, active interaction, vehicle, projectile or nearby character.
    const auto& lod=config_.simulationLod;
    auto* diagnostics=lod.enabled?w.resource<world::SimulationLodDiagnostics>():nullptr;
    world::SimulationLodTier tier=world::SimulationLodTier::Hot;
    if(lod.enabled&&bot.initialized&&bot.life==a.life&&bot.slot==a.slot&&bot.lastTick<now&&
       (bot.decision==lib::bots::ActorDecision::Idle||bot.decision==lib::bots::ActorDecision::Patrol)&&
       !bot.target&&!a.vehicle&&!a.downed&&!a.frozen&&!a.fireHeld&&!a.blockHeld&&!a.lockTarget&&
       a.health==100&&a.blindUntil<=now&&a.reloadUntil<=now&&a.switchUntil<=now&&a.staggerUntil<=now&&
       a.activeAction==Action::None&&a.input.action==Action::None&&!a.input.argument&&!a.input.buttons&&
       !s.royale.enabled&&!s.match.planted&&w.countExisting<Projectile>()==0){
        const World& view=w;
        const world::SpatialWorld cells(32);
        const auto here=cells.cell(a.pos);
        std::vector<world::Cell> observers;
        Fixed range=fxMax(host.config.sightRange,config_.systems.interactRange);
        range=fxMax(range,config_.melee.lockRange);
        for(const auto& weapon:config_.weapons)range=fxMax(range,weapon.range);
        // Conservative guard: a full interaction range plus two cells.
        const uint64_t guard=uint64_t(range.raw/(32*Fixed::kOne))+2;
        bool critical=false;
        const Simulation& read=sim;
        forEachCharacter(read,[&](ConstCharacterRef other){
            if(other.slot==a.slot||other.health<=0)return;
            const auto cell=cells.cell(other.pos);
            if(!bots_[other.slot-1]||other.lastSequence)observers.push_back(cell);
            if(world::cellDistance(cell,here)<=guard)critical=true;
        });
        // Interior/stair traversal and nearby loot are not decimated.
        for(const auto& region:buildingRegions_)if(insideXZ(region.bounds,a.pos)){critical=true;break;}
        view.eachExistingConst<Loot>([&](Entity,const Loot& item){
            if(world::cellDistance(cells.cell(item.pos),here)<=2)critical=true;
        });
        view.eachExistingConst<Vehicle>([&](Entity,const Vehicle& car){
            if(world::cellDistance(cells.cell(car.pos),here)<=2)critical=true;
        });
        uint64_t id=a.slot; // stable host-level slot, never an ECS index
        if(view.resourceConst<world::RegionEntityLedger>()){
            const auto* identity=view.tryGetConst<world::RegionStableIdentity>(a.entity);
            id=identity?identity->id:0;
        }
        const auto decision=world::simulationLodDecision(lod,here,observers,id,now,critical);
        tier=decision.tier;
        if(!decision.due&&now-bot.lastTick<decision.period){
            if(diagnostics){
                if(tier==world::SimulationLodTier::Warm)++diagnostics->warm;
                else ++diagnostics->cold;
                ++diagnostics->deferred;
            }
            // Retain only continuous steering already present in canonical
            // CharacterControl. No stale fire/reload/jump/interaction edge.
            a.input.viewTick=now;a.lastInputTick=now;
            return true;
        }
    }
    if(diagnostics){
        switch(tier){
            case world::SimulationLodTier::Hot:++diagnostics->hot;break;
            case world::SimulationLodTier::Warm:++diagnostics->warm;break;
            case world::SimulationLodTier::Cold:++diagnostics->cold;break;
        }
        ++diagnostics->executed;
    }
    // Objective: wanted loot within 25 m (closest), else a point inside the next circle (spread by slot).
    std::optional<Vec3> objective;std::optional<Loot> want;Fixed best=625_fx;
    const auto* loadout=gear(sim,a.slot);
    for(const auto& l:loot(sim)){
        auto d=(l.pos-a.pos).lengthSq();if(d>best)continue;bool useful=false;
        if(l.type>=200){if(loadout){lib::systems::GearPiece piece{l.type-200,l.ammo};auto it=config_.gear.find(piece.def);
            if(it!=config_.gear.end()){auto cur=loadout->slots.find(it->second.slot);useful=lib::systems::betterGear(config_.gear,piece,cur==loadout->slots.end()?nullptr:&cur->second);}}}
        else if(l.type>=100)useful=!(a.owned&(1u<<uint32_t(l.type-100)));
        else useful=l.type==1||l.type==9||(l.type==2&&a.armor<50);
        if(useful){best=d;want=l;}
    }
    if(want)objective=want->pos;
    else if(s.royale.enabled){auto t=lib::mechanics::safeZoneTarget(config_.royale->zone,s.royale.zone);
        auto turn=Fixed::fromRatio(int64_t(a.slot)*37%100,100);auto r=t.radius*Fixed::fromRatio(int64_t(a.slot)%5+1,8);
        objective=Vec3{t.center.x+fxSinTurns(turn)*r,a.pos.y,t.center.y+fxCosTurns(turn)*r};}
    // Navigation: the building mesh the bot stands in; a goal inside another building goes through its door.
    // The navmesh covers each floor; flights of stairs are crossed by walking straight from one end to the other.
    const lib::bots::NavMesh* mesh=nullptr;const BuildingRegion* region=nullptr;
    for(size_t i=0;i<buildingRegions_.size();++i)if(insideXZ(buildingRegions_[i].bounds,a.pos)){mesh=&host.meshes[i];region=&buildingRegions_[i];}
    if(objective&&!mesh)for(const auto& r:buildingRegions_)if(insideXZ(r.bounds,*objective)){objective=r.door;break;}
    // Still on a flight (between its ends, inside its lane): finish it toward the objective's height first.
    if(objective&&region)for(const auto& [bottom,top]:region->stairs){
        if(a.pos.y<bottom.y-Fixed::fromRatio(1,10)||a.pos.y>top.y+Fixed::fromRatio(1,10))continue;
        auto lane=top-bottom;lane.y={};auto along=a.pos-bottom;along.y={};auto t=along.dot(lane)/lane.lengthSq();
        if(t<=Fixed::fromRatio(1,20)||t>=Fixed::fromRatio(19,20))continue;   // at an end: back on the floor's navmesh
        auto off=bottom+lane*t-a.pos;off.y={};if(off.length()>Fixed::fromRatio(6,10))continue;
        auto d=(objective->y>=a.pos.y?top:bottom)-a.pos;auto yaw=fxAtan2Turns(d.x,d.z);if(yaw<Fixed{})yaw+=1_fx;
        Input step;step.yaw=yaw;step.forward=1000;step.viewTick=now;a.input=step;a.lastInputTick=now;return true;
    }
    if(objective&&region&&insideXZ(region->bounds,*objective)&&fxAbs(objective->y-a.pos.y)>Fixed::fromRatio(1,2)){
        bool up=objective->y>a.pos.y;
        for(size_t k=0;k<region->stairs.size();++k){const auto& [bottom,top]=region->stairs[k];
            auto from=up?bottom:top,to=up?top:bottom;if(fxAbs(from.y-a.pos.y)>Fixed::fromRatio(1,2)&&!(a.pos.y>bottom.y&&a.pos.y<top.y))continue;
            auto lane=(to-from);lane.y={};auto along=(a.pos-from);along.y={};auto t=lane.lengthSq().raw?along.dot(lane)/lane.lengthSq():Fixed{};
            auto offLane=(from+lane*fxClamp(t,Fixed{},1_fx)-a.pos);offLane.y={};
            bool onFlight=t>=-Fixed::fromRatio(1,10)&&t<=1_fx&&offLane.length()<Fixed::fromRatio(1,2);
            // The navmesh reaches a hall point beside the stairwell (clear of the walls); from there walk straight to
            // the first step and up / down the flight.
            auto hall=up?region->halls[k].first:region->halls[k].second;
            auto toFrom=(from-a.pos).xz().length();
            if(onFlight||toFrom<Fixed::fromRatio(3,10)||(hall-a.pos).xz().length()<Fixed::fromRatio(5,10)||toFrom<(hall-from).xz().length()){
                auto target=onFlight||toFrom<Fixed::fromRatio(3,10)?to:from;auto d=target-a.pos;auto yaw=fxAtan2Turns(d.x,d.z);if(yaw<Fixed{})yaw+=1_fx;
                Input step;step.yaw=yaw;step.forward=1000;step.viewTick=now;a.input=step;a.lastInputTick=now;return true;
            }
            objective=hall;break;
        }
    }
    // On the landing strip of a flight's end (narrow, next to the stairwell hole): walk to that end's hall first.
    if(region)for(size_t k=0;k<region->stairs.size();++k){
        const auto& [bottom,top]=region->stairs[k];
        for(int e=0;e<2;++e){
            auto end=e?top:bottom,other=e?bottom:top;auto hall=e?region->halls[k].second:region->halls[k].first;
            if(fxAbs(end.y-a.pos.y)>Fixed::fromRatio(3,10))continue;
            auto out=end-other;out.y={};auto n=fxNormalize(out);auto rel=a.pos-end;rel.y={};
            auto along=rel.dot(n),side=rel.x*n.z-rel.z*n.x;
            if(along< -Fixed::fromRatio(1,2)||along>Fixed::fromRatio(12,10)||fxAbs(side)>Fixed::fromRatio(8,10))continue;
            auto d=hall-a.pos;d.y={};if(d.length()<Fixed::fromRatio(3,10))continue;
            auto yaw=fxAtan2Turns(d.x,d.z);if(yaw<Fixed{})yaw+=1_fx;
            Input step;step.yaw=yaw;step.forward=1000;step.viewTick=now;a.input=step;a.lastInputTick=now;return true;
        }
    }
    auto command=lib::bots::fpsCommand(sim,*this,a.slot,host.config,{},bot,objective,mesh);
    if(!command)return true;   // keep the previous input this tick
    Input input;if(!decodeInput(*command,input))return true;
    // Pick up when the wanted item is in reach; heal when hurt and nobody is in sight.
    if(want&&(want->pos-a.pos).length()<=config_.systems.interactRange-Fixed::fromRatio(1,2)&&!(input.buttons&Fire))input.action=Action::Interact;
    else if(config_.systems.enabled&&a.health<60&&bot.decision!=lib::bots::ActorDecision::Engage&&a.activeAction==Action::None)
        for(auto item:a.bag)if(item.type==1){input.action=Action::Heal;input.forward=input.right=0;break;}
    input.sequence=0;a.input=input;a.lastInputTick=now;
    if(auto* memory=w.tryGet<BotMemory>(a.entity))memory->bytes=lib::bots::saveFps(bot);
    return true;
}
}
