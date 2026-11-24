// Bounded read-only completed local ECS snapshot, never a physics owner.
#pragma once
#include "aiverselib/world/EvolvePlanetDeadwood.h"
#include "aiverselib/world/PlanetConsequenceOwner.h"
#include "engine/world/TerraWeaveForestHarvestEcs.h"
namespace aiverse::studio {
inline Json sampleStudioPlanetForest(const World& ecs,const game::Surface& surface,uint32_t tick){
    namespace tw=terraweave;namespace ev=evolve;namespace wp=lib::worldprocess;
    Json out=Json::object(),rows=Json::array();
    const auto* roots=ecs.resourceConst<tw::TerraWeaveAuthorityResource>();
    const auto* bindings=ecs.resourceConst<ev::PlanetRootDeliveryResource>();
    const auto* history=ecs.resourceConst<tw::ForestEcologyResource>();
    const auto* wood=ecs.resourceConst<ev::PlanetDeadwoodResource>();
    const auto* physics=ecs.resourceConst<wp::PlanetConsequenceResource>();
    const auto* harvest=ecs.resourceConst<tw::ForestHarvestResource>();
    const bool active=roots&&bindings&&wood&&surface.tab&&roots->rooted.size()==bindings->bindings.size()&&
        roots->rooted.size()<=ev::kMaxPlanetRootBindings&&bindings->events.size()<=ev::kMaxPlanetPlantFallEvents&&
        (!physics||physics->bodies.size()<=wp::kMaxConsequences)&&ev::validPlanetDeadwood(*wood,surface);
    out.set("active",Json::fromBool(active));if(!active)return out;
    out.set("tick",Json::number(int64_t(tick)));
    std::map<tw::AnchoredPlantId,const ev::PlanetRootBinding*> mapped;
    for(const auto& b:bindings->bindings)mapped.emplace(b.id,&b);
    std::set<tw::AnchoredPlantId> dead;
    if(history)for(const auto& h:history->plants)if(h.dead)dead.insert(h.id);
    std::map<wp::ConsequenceKey,const wp::ConsequenceBody*> bodies;
    if(physics)for(const auto& body:physics->bodies)bodies.emplace(body.key,&body);
    std::map<tw::AnchoredPlantId,const ev::PlanetPlantFallRecord*> current;
    size_t pending=0;
    for(const auto& r:bindings->events){
        if(!r.acknowledged)++pending;
        const auto id=r.fall.event.id;const auto b=mapped.find(id);
        if(b==mapped.end()||r.fall.bodyId!=b->second->bodyId||r.fall.plantedPosition!=b->second->plantedPosition)continue;
        const auto old=current.find(id);
        if(old==current.end()||ev::planetFallKey(old->second->fall)<ev::planetFallKey(r.fall))current[id]=&r;
    }
    size_t fallen=0,spent=0,physical=0,rejected=0;
    for(const auto& plant:roots->rooted){
        const tw::AnchoredPlantId id{plant.anchor.speciesId,plant.anchor.plantKey};
        const auto b=mapped.find(id);if(b==mapped.end()){++rejected;continue;}
        const auto frame=wp::surfaceFrame(&surface,b->second->plantedPosition);
        if(!frame||surface.bodies[size_t(frame->body)].id!=b->second->bodyId){++rejected;continue;}
        const auto log=std::lower_bound(wood->ledger.logs.begin(),wood->ledger.logs.end(),id,
            [](const auto& r,auto key){return r.id<key;});
        const bool hasWood=log!=wood->ledger.logs.end()&&log->id==id;
        const bool isSpent=hasWood&&!log->remainingUnits;
        const auto receipt=current.find(id);
        const auto body=receipt!=current.end()&&receipt->second->acknowledged?
            bodies.find(wp::consequenceKey(receipt->second->fall)):bodies.end();
        const bool hasBody=body!=bodies.end()&&body->second->body==b->second->bodyId;
        fallen+=plant.fallen;spent+=isSpent;physical+=hasBody;
        if(rows.size()>=128)continue;
        Json row=Json::object();
        row.set("species",Json::number(int64_t(id.speciesId)));row.set("key",Json::string(std::to_string(id.key)));
        row.set("body",Json::string(b->second->bodyId));row.set("cell",Json::number(int64_t(frame->cell)));
        row.set("ageTicks",Json::string(std::to_string(plant.environment.ageTicks)));
        row.set("supportPermille",Json::number(int64_t(plant.support.supportPermille)));
        row.set("dead",Json::fromBool(dead.contains(id)));row.set("fallen",Json::fromBool(plant.fallen));
        row.set("harvested",Json::fromBool(harvest&&tw::isForestHarvested(*harvest,id)));
        row.set("spent",Json::fromBool(isSpent));row.set("physical",Json::fromBool(hasBody));
        row.set("remainingUnits",hasWood?Json::string(std::to_string(log->remainingUnits)):Json{});
        row.set("initialUnits",hasWood?Json::string(std::to_string(log->initialUnits)):Json{});
        if(receipt!=current.end())row.set("pending",Json::fromBool(!receipt->second->acknowledged));
        if(hasBody){
            row.set("motionTick",body->second->lastMotionTick==UINT32_MAX?Json{}:Json::number(int64_t(body->second->lastMotionTick)));
            row.set("awake",Json::fromBool(body->second->motion.awake));
        }
        rows.push(std::move(row));
    }
    out.set("plantCount",Json::number(int64_t(roots->rooted.size())));
    out.set("deadCount",Json::number(int64_t(dead.size())));
    out.set("fallenCount",Json::number(int64_t(fallen)));out.set("spentCount",Json::number(int64_t(spent)));
    out.set("physicalCount",Json::number(int64_t(physical)));out.set("pendingCount",Json::number(int64_t(pending)));
    out.set("rejected",Json::number(int64_t(rejected)));
    out.set("truncated",Json::fromBool(roots->rooted.size()>128));
    out.set("plants",std::move(rows));return out;
}
} // namespace aiverse::studio
